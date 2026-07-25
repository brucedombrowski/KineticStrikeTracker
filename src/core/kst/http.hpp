// kst::http — bounded HTTPS retrieval over the OS-provided libcurl.
//
// REQ-9.3: network access lives ONLY in this layer and the source adapters
// built on it. The analysis core never reaches the network.
// REQ-9.4: libcurl ships with macOS — an OS-provided library, not a
// third-party dependency.
//
// Security posture (all enforced, none optional):
// - REQ-10.5 / RFC 8446: TLS with peer and host verification always on;
//   TLS 1.2 floor, 1.3 preferred. Verification cannot be disabled through
//   this interface — there is no flag for it.
// - REQ-12.8: only https:// accepted; redirects may not downgrade to
//   non-TLS; response size, redirect count, and timeouts are bounded.
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>

namespace kst::http {

struct Limits {
    std::size_t max_response_bytes = 64u * 1024u * 1024u;  // 64 MiB
    long max_redirects = 5;
    long connect_timeout_seconds = 15;
    long total_timeout_seconds = 300;  // catalog queries can be slow
};

struct Response {
    long status = 0;
    std::string body;
    std::string final_url;      // after any permitted redirects
    std::string content_type;
    std::string sha256;         // hex digest of body (REQ-2.10)
    std::string requested_at;   // ISO 8601 UTC, when the request was issued
};

struct Error {
    std::string message;
    long status = 0;  // non-zero if an HTTP status was received
};

struct Result {
    std::optional<Response> response;
    std::optional<Error> error;
    explicit operator bool() const { return response.has_value(); }
    const Response& operator*() const { return *response; }
    const Response* operator->() const { return &*response; }
};

// Perform a GET. `url` MUST be https://. A non-2xx status is returned as an
// Error carrying the status, not as a successful Response.
Result get(const std::string& url, const Limits& limits = {},
           const std::map<std::string, std::string>& headers = {});

// SHA-256 over arbitrary bytes, lowercase hex. Exposed because the file
// ingestion path needs the same digest (REQ-12.10) without an HTTP request.
std::string sha256_hex(std::string_view data);

}  // namespace kst::http
