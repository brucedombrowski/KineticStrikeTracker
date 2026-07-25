#include "kst/http.hpp"

#include <curl/curl.h>

#include <array>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <string_view>

// SHA-256 via the OS-provided CommonCrypto (macOS). Isolated here so a port
// swaps one function, not the call sites (REQ-9.5).
#include <CommonCrypto/CommonDigest.h>

namespace kst::http {

std::string sha256_hex(std::string_view data) {
    std::array<unsigned char, CC_SHA256_DIGEST_LENGTH> digest{};
    CC_SHA256(data.data(), static_cast<CC_LONG>(data.size()), digest.data());
    std::string hex;
    hex.reserve(digest.size() * 2);
    static constexpr char kHex[] = "0123456789abcdef";
    for (unsigned char b : digest) {
        hex.push_back(kHex[b >> 4]);
        hex.push_back(kHex[b & 0x0F]);
    }
    return hex;
}

namespace {

// libcurl global init is not thread-safe; do it exactly once.
void ensure_global_init() {
    static std::once_flag once;
    std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

std::string utc_now_iso8601() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now.time_since_epoch()) %
                    1000;
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour,
                  tm.tm_min, tm.tm_sec, static_cast<int>(ms.count()));
    return buf;
}

struct Sink {
    std::string body;
    std::size_t limit = 0;
    bool overflowed = false;
};

std::size_t write_cb(char* ptr, std::size_t size, std::size_t nmemb,
                     void* userdata) {
    Sink* sink = static_cast<Sink*>(userdata);
    const std::size_t n = size * nmemb;
    if (sink->body.size() + n > sink->limit) {
        sink->overflowed = true;
        return 0;  // abort the transfer (REQ-12.8: bounded response size)
    }
    sink->body.append(ptr, n);
    return n;
}

bool is_https(const std::string& url) {
    return url.rfind("https://", 0) == 0;
}

struct CurlHandle {
    CURL* h = curl_easy_init();
    CurlHandle() = default;
    ~CurlHandle() {
        if (h) curl_easy_cleanup(h);
    }
    CurlHandle(const CurlHandle&) = delete;
    CurlHandle& operator=(const CurlHandle&) = delete;
};

struct SList {
    curl_slist* h = nullptr;
    SList() = default;
    ~SList() {
        if (h) curl_slist_free_all(h);
    }
    SList(const SList&) = delete;
    SList& operator=(const SList&) = delete;
};

}  // namespace

Result get(const std::string& url, const Limits& limits,
           const std::map<std::string, std::string>& headers) {
    // REQ-12.8: scheme is checked before any network activity.
    if (!is_https(url)) {
        return {std::nullopt,
                Error{"refusing non-HTTPS URL: " + url, 0}};
    }
    ensure_global_init();
    CurlHandle curl;
    if (!curl.h) {
        return {std::nullopt, Error{"curl_easy_init failed", 0}};
    }

    Sink sink;
    sink.limit = limits.max_response_bytes;
    const std::string requested_at = utc_now_iso8601();

    curl_easy_setopt(curl.h, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl.h, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl.h, CURLOPT_WRITEDATA, &sink);

    // TLS: verification on, and no interface exists to turn it off (REQ-10.5).
    curl_easy_setopt(curl.h, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl.h, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl.h, CURLOPT_SSLVERSION,
                     static_cast<long>(CURL_SSLVERSION_TLSv1_2));

    // Redirects: bounded, and never off HTTPS (REQ-12.8).
    curl_easy_setopt(curl.h, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl.h, CURLOPT_MAXREDIRS, limits.max_redirects);
    curl_easy_setopt(curl.h, CURLOPT_REDIR_PROTOCOLS_STR, "https");

    curl_easy_setopt(curl.h, CURLOPT_CONNECTTIMEOUT,
                     limits.connect_timeout_seconds);
    curl_easy_setopt(curl.h, CURLOPT_TIMEOUT, limits.total_timeout_seconds);
    curl_easy_setopt(curl.h, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl.h, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl.h, CURLOPT_USERAGENT,
                     "KineticStrikeTracker/0.3 (+https://github.com/"
                     "brucedombrowski/KineticStrikeTracker)");

    SList hdrs;
    for (const auto& [k, v] : headers) {
        hdrs.h = curl_slist_append(hdrs.h, (k + ": " + v).c_str());
    }
    if (hdrs.h) curl_easy_setopt(curl.h, CURLOPT_HTTPHEADER, hdrs.h);

    const CURLcode rc = curl_easy_perform(curl.h);
    if (rc != CURLE_OK) {
        if (sink.overflowed) {
            return {std::nullopt,
                    Error{"response exceeds max_response_bytes limit", 0}};
        }
        return {std::nullopt, Error{curl_easy_strerror(rc), 0}};
    }

    Response out;
    curl_easy_getinfo(curl.h, CURLINFO_RESPONSE_CODE, &out.status);
    char* eff = nullptr;
    curl_easy_getinfo(curl.h, CURLINFO_EFFECTIVE_URL, &eff);
    if (eff) out.final_url = eff;
    char* ctype = nullptr;
    curl_easy_getinfo(curl.h, CURLINFO_CONTENT_TYPE, &ctype);
    if (ctype) out.content_type = ctype;

    if (out.status < 200 || out.status >= 300) {
        return {std::nullopt,
                Error{"HTTP status " + std::to_string(out.status),
                      out.status}};
    }

    out.body = std::move(sink.body);
    out.sha256 = sha256_hex(out.body);  // REQ-2.10
    out.requested_at = requested_at;
    return {std::move(out), std::nullopt};
}

}  // namespace kst::http
