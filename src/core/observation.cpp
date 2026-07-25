#include "kst/observation.hpp"

#include <CommonCrypto/CommonDigest.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace kst::model {

const char* to_string(SourceClass c) {
    switch (c) {
        case SourceClass::Instrumental:   return "A";
        case SourceClass::Government:     return "B";
        case SourceClass::CuratedDataset: return "C";
        case SourceClass::News:           return "D";
        case SourceClass::Social:         return "E";
    }
    return "?";
}

std::optional<SourceClass> source_class_from_string(std::string_view s) {
    if (s == "A") return SourceClass::Instrumental;
    if (s == "B") return SourceClass::Government;
    if (s == "C") return SourceClass::CuratedDataset;
    if (s == "D") return SourceClass::News;
    if (s == "E") return SourceClass::Social;
    return std::nullopt;
}

std::string observation_uid(std::string_view source_id,
                            std::string_view native_id) {
    // RFC 9562 s5.5: UUIDv5 = SHA-1(namespace || name), with version and
    // variant bits set. Namespace: a fixed project UUID, so identifiers are
    // stable across machines and runs. SHA-1 here is a naming function, not
    // a security primitive.
    static constexpr std::array<unsigned char, 16> kNamespace = {
        0x6b, 0x73, 0x74, 0x2d, 0x6f, 0x62, 0x73, 0x76,
        0x2d, 0x6e, 0x73, 0x2d, 0x76, 0x35, 0x30, 0x31};

    std::string name;
    name.reserve(source_id.size() + native_id.size() + 1);
    name.append(source_id);
    name.push_back('\0');  // unambiguous separator
    name.append(native_id);

    CC_SHA1_CTX ctx;
    CC_SHA1_Init(&ctx);
    CC_SHA1_Update(&ctx, kNamespace.data(),
                   static_cast<CC_LONG>(kNamespace.size()));
    CC_SHA1_Update(&ctx, name.data(), static_cast<CC_LONG>(name.size()));
    std::array<unsigned char, CC_SHA1_DIGEST_LENGTH> d{};
    CC_SHA1_Final(d.data(), &ctx);

    d[6] = static_cast<unsigned char>((d[6] & 0x0F) | 0x50);  // version 5
    d[8] = static_cast<unsigned char>((d[8] & 0x3F) | 0x80);  // RFC variant

    char buf[37];
    std::snprintf(buf, sizeof(buf),
                  "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-"
                  "%02x%02x%02x%02x%02x%02x",
                  d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7], d[8], d[9],
                  d[10], d[11], d[12], d[13], d[14], d[15]);
    return buf;
}

namespace {

bool read_int(std::string_view s, std::size_t pos, std::size_t len, int& out) {
    if (pos + len > s.size()) return false;
    int v = 0;
    for (std::size_t i = 0; i < len; ++i) {
        const char c = s[pos + i];
        if (c < '0' || c > '9') return false;
        v = v * 10 + (c - '0');
    }
    out = v;
    return true;
}

// Days from civil date (Howard Hinnant's algorithm) — avoids timegm, which
// is not portable and consults the environment.
std::int64_t days_from_civil(int y, int m, int d) {
    y -= m <= 2;
    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const auto yoe = static_cast<unsigned>(y - era * 400);
    const auto doy = static_cast<unsigned>((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 +
                                           d - 1);
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
}

void civil_from_days(std::int64_t z, int& y, unsigned& m, unsigned& d) {
    z += 719468;
    const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const auto doe = static_cast<unsigned long>(z - era * 146097);
    const unsigned yoe =
        static_cast<unsigned>((doe - doe / 1460 + doe / 36524 - doe / 146096) / 365);
    const std::int64_t yr = static_cast<std::int64_t>(yoe) + era * 400;
    const unsigned doy = static_cast<unsigned>(doe - (365 * yoe + yoe / 4 - yoe / 100));
    const unsigned mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp + (mp < 10 ? 3 : static_cast<unsigned>(-9));
    y = static_cast<int>(yr + (m <= 2));
}

}  // namespace

std::optional<std::int64_t> parse_iso8601_ms(std::string_view t) {
    // YYYY-MM-DDTHH:MM:SS[.fff][Z|±HH:MM]
    int year, mon, day, hour, min, sec;
    if (t.size() < 19) return std::nullopt;
    if (!read_int(t, 0, 4, year) || t[4] != '-' || !read_int(t, 5, 2, mon) ||
        t[7] != '-' || !read_int(t, 8, 2, day)) {
        return std::nullopt;
    }
    if (t[10] != 'T' && t[10] != ' ') return std::nullopt;
    if (!read_int(t, 11, 2, hour) || t[13] != ':' || !read_int(t, 14, 2, min) ||
        t[16] != ':' || !read_int(t, 17, 2, sec)) {
        return std::nullopt;
    }
    if (mon < 1 || mon > 12 || day < 1 || day > 31 || hour > 23 || min > 59 ||
        sec > 60) {
        return std::nullopt;
    }

    std::size_t pos = 19;
    int millis = 0;
    if (pos < t.size() && t[pos] == '.') {
        ++pos;
        int digits = 0;
        while (pos < t.size() && t[pos] >= '0' && t[pos] <= '9') {
            if (digits < 3) millis = millis * 10 + (t[pos] - '0');
            ++digits;
            ++pos;
        }
        for (int i = digits; i < 3; ++i) millis *= 10;
    }

    // REQ-3.2: no timezone designator means we refuse rather than assume.
    std::int64_t offset_seconds = 0;
    if (pos >= t.size()) return std::nullopt;
    if (t[pos] == 'Z' || t[pos] == 'z') {
        ++pos;
    } else if (t[pos] == '+' || t[pos] == '-') {
        const int sign = (t[pos] == '-') ? -1 : 1;
        int oh = 0, om = 0;
        if (!read_int(t, pos + 1, 2, oh)) return std::nullopt;
        std::size_t mpos = pos + 3;
        if (mpos < t.size() && t[mpos] == ':') ++mpos;
        if (!read_int(t, mpos, 2, om)) return std::nullopt;
        offset_seconds = sign * (oh * 3600 + om * 60);
        pos = mpos + 2;
    } else {
        return std::nullopt;
    }
    if (pos != t.size()) return std::nullopt;

    const std::int64_t days = days_from_civil(year, mon, day);
    const std::int64_t secs =
        days * 86400 + hour * 3600 + min * 60 + sec - offset_seconds;
    return secs * 1000 + millis;
}

std::string format_iso8601_ms(std::int64_t epoch_ms) {
    std::int64_t secs = epoch_ms / 1000;
    int ms = static_cast<int>(epoch_ms % 1000);
    if (ms < 0) {
        ms += 1000;
        --secs;
    }
    std::int64_t days = secs / 86400;
    std::int64_t rem = secs % 86400;
    if (rem < 0) {
        rem += 86400;
        --days;
    }
    int y;
    unsigned mo, d;
    civil_from_days(days, y, mo, d);
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%04d-%02u-%02uT%02d:%02d:%02d.%03dZ", y, mo,
                  d, static_cast<int>(rem / 3600),
                  static_cast<int>((rem % 3600) / 60),
                  static_cast<int>(rem % 60), ms);
    return buf;
}

double normalise_longitude(double lon) {
    while (lon > 180.0) lon -= 360.0;
    while (lon < -180.0) lon += 360.0;
    return lon;
}

bool latitude_valid(double lat) { return lat >= -90.0 && lat <= 90.0; }

double haversine_km(double lat1, double lon1, double lat2, double lon2) {
    constexpr double kR = 6371.0088;  // mean Earth radius
    constexpr double kDeg = 3.14159265358979323846 / 180.0;
    const double dlat = (lat2 - lat1) * kDeg;
    const double dlon = (lon2 - lon1) * kDeg;
    const double a = std::sin(dlat / 2) * std::sin(dlat / 2) +
                     std::cos(lat1 * kDeg) * std::cos(lat2 * kDeg) *
                         std::sin(dlon / 2) * std::sin(dlon / 2);
    return 2 * kR * std::asin(std::min(1.0, std::sqrt(a)));
}

}  // namespace kst::model
