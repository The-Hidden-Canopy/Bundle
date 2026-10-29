#include "bundle/persistence/text.hpp"

#include <array>
#include <chrono>
#include <cstdio>

namespace bundle::persistence {
namespace {

constexpr std::array<std::uint32_t, 64> k_round = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

constexpr std::uint32_t rotr(const std::uint32_t x, const int n) { return (x >> n) | (x << (32 - n)); }

}  // namespace

std::string sha256_hex(const std::string_view bytes) {
    std::array<std::uint32_t, 8> h = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                      0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::string data(bytes);
    const std::uint64_t bit_length = static_cast<std::uint64_t>(bytes.size()) * 8;
    data.push_back(static_cast<char>(0x80));
    while (data.size() % 64 != 56) data.push_back('\0');
    for (int i = 7; i >= 0; --i) data.push_back(static_cast<char>((bit_length >> (i * 8)) & 0xFF));
    for (std::size_t chunk = 0; chunk < data.size(); chunk += 64) {
        std::array<std::uint32_t, 64> w{};
        for (int i = 0; i < 16; ++i) {
            const auto* p = reinterpret_cast<const unsigned char*>(data.data() + chunk + i * 4);
            w[i] = (std::uint32_t{p[0]} << 24) | (std::uint32_t{p[1]} << 16) |
                   (std::uint32_t{p[2]} << 8) | std::uint32_t{p[3]};
        }
        for (int i = 16; i < 64; ++i) {
            const auto s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const auto s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        auto [a, b, c, d, e, f, g, hh] = h;
        for (int i = 0; i < 64; ++i) {
            const auto s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const auto ch = (e & f) ^ (~e & g);
            const auto t1 = hh + s1 + ch + k_round[i] + w[i];
            const auto s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const auto maj = (a & b) ^ (a & c) ^ (b & c);
            const auto t2 = s0 + maj;
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    std::string out;
    char buffer[9];
    for (const auto word : h) {
        std::snprintf(buffer, sizeof(buffer), "%08x", word);
        out += buffer;
    }
    return out;
}

std::string sha256_tagged(const std::string_view bytes) { return "sha256:" + sha256_hex(bytes); }

bool is_sha256_digest(const std::string_view value) noexcept {
    if (value.size() != 71 || value.substr(0, 7) != "sha256:") return false;
    for (const char c : value.substr(7)) {
        const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        if (!hex) return false;
    }
    return true;
}

// -- timestamps -------------------------------------------------------------------

namespace {

struct Cursor final {
    std::string_view text;
    std::size_t at = 0;

    [[nodiscard]] bool done() const { return at >= text.size(); }
    [[nodiscard]] char peek() const { return done() ? '\0' : text[at]; }
    bool digits(const std::size_t count, int& out) {
        if (at + count > text.size()) return false;
        int value = 0;
        for (std::size_t i = 0; i < count; ++i) {
            const char c = text[at + i];
            if (c < '0' || c > '9') return false;
            value = value * 10 + (c - '0');
        }
        at += count;
        out = value;
        return true;
    }
    [[nodiscard]] bool is_digit() const { return !done() && text[at] >= '0' && text[at] <= '9'; }
};

// Reads ".ffffff" / ",ffffff" (at least one digit) -> microseconds (truncated).
bool fraction(Cursor& cursor, int& micros) {
    micros = 0;
    if (cursor.peek() != '.' && cursor.peek() != ',') return true;
    ++cursor.at;
    if (!cursor.is_digit()) return false;
    int count = 0;
    while (cursor.is_digit()) {
        if (count < 6) micros = micros * 10 + (cursor.peek() - '0');
        ++count;
        ++cursor.at;
    }
    for (int i = count; i < 6; ++i) micros *= 10;
    return true;
}

// HH[:MM[:SS[frac]]] or HH[MM[SS[frac]]]; returns false on malformed input.
bool clock(Cursor& cursor, int& hour, int& minute, int& second, int& micros) {
    minute = second = micros = 0;
    if (!cursor.digits(2, hour)) return false;
    if (cursor.peek() == ':') {
        ++cursor.at;
        if (!cursor.digits(2, minute)) return false;
        if (cursor.peek() == ':') {
            ++cursor.at;
            if (!cursor.digits(2, second)) return false;
            return fraction(cursor, micros);
        }
        return true;
    }
    if (cursor.is_digit()) {
        if (!cursor.digits(2, minute)) return false;
        if (cursor.is_digit()) {
            if (!cursor.digits(2, second)) return false;
            return fraction(cursor, micros);
        }
    }
    return true;
}

}  // namespace

std::string format_utc_micros(const std::int64_t unix_micros) {
    using namespace std::chrono;
    const sys_time<microseconds> point{microseconds{unix_micros}};
    const auto day_point = floor<days>(point);
    const year_month_day ymd{day_point};
    const hh_mm_ss<microseconds> tod{point - day_point};
    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), "%04d-%02u-%02uT%02lld:%02lld:%02lld",
                  static_cast<int>(ymd.year()), static_cast<unsigned>(ymd.month()),
                  static_cast<unsigned>(ymd.day()), static_cast<long long>(tod.hours().count()),
                  static_cast<long long>(tod.minutes().count()),
                  static_cast<long long>(tod.seconds().count()));
    std::string out = buffer;
    const auto micros = tod.subseconds().count();
    if (micros != 0) {
        std::snprintf(buffer, sizeof(buffer), ".%06lld", static_cast<long long>(micros));
        out += buffer;
    }
    return out + "Z";
}

TimestampParse parse_timestamp(const std::string_view value) {
    using namespace std::chrono;
    TimestampParse result;
    Cursor cursor{value};
    int y = 0, mo = 0, d = 0;
    if (!cursor.digits(4, y)) return result;
    if (cursor.peek() == '-') {
        ++cursor.at;
        if (!cursor.digits(2, mo) || cursor.peek() != '-') return result;
        ++cursor.at;
        if (!cursor.digits(2, d)) return result;
    } else if (!cursor.digits(2, mo) || !cursor.digits(2, d)) {
        return result;
    }
    const year_month_day date{year{y}, month{static_cast<unsigned>(mo)}, day{static_cast<unsigned>(d)}};
    if (!date.ok() || y < 1) return result;
    if (cursor.done()) {
        result.problem = TimestampProblem::naive;
        return result;
    }
    if (cursor.peek() != 'T' && cursor.peek() != ' ') return result;
    ++cursor.at;
    int hour = 0, minute = 0, second = 0, micros = 0;
    if (!clock(cursor, hour, minute, second, micros)) return result;
    if (hour > 23 || minute > 59 || second > 59) return result;
    if (cursor.peek() == ' ') ++cursor.at;  // Python accepts one space before the offset
    if (cursor.done()) {
        result.problem = TimestampProblem::naive;
        return result;
    }
    std::int64_t offset_micros = 0;
    const char sign = cursor.peek();
    if (sign == 'Z') {
        ++cursor.at;
    } else if (sign == '+' || sign == '-') {
        ++cursor.at;
        int oh = 0, om = 0, os = 0, ou = 0;
        if (!cursor.digits(2, oh)) return result;
        if (cursor.peek() == ':') {
            ++cursor.at;
            if (!cursor.digits(2, om)) return result;
            if (cursor.peek() == ':') {
                ++cursor.at;
                if (!cursor.digits(2, os)) return result;
                if (cursor.peek() == '.') {
                    if (!fraction(cursor, ou)) return result;
                }
            }
        } else if (cursor.is_digit()) {
            if (!cursor.digits(2, om)) return result;
        }
        if (om > 59 || os > 59) return result;
        offset_micros = ((static_cast<std::int64_t>(oh) * 3600 + om * 60 + os) * 1'000'000 + ou);
        if (offset_micros >= 24LL * 3600 * 1'000'000) return result;
        if (sign == '-') offset_micros = -offset_micros;
    } else {
        return result;
    }
    if (!cursor.done()) return result;
    const auto local = sys_days{date}.time_since_epoch();
    const std::int64_t local_micros =
        duration_cast<microseconds>(local).count() +
        ((static_cast<std::int64_t>(hour) * 3600 + minute * 60 + second) * 1'000'000) + micros;
    const std::int64_t utc = local_micros - offset_micros;
    // datetime supports years 1..9999 after conversion to UTC.
    const auto utc_day = year_month_day{floor<days>(sys_time<microseconds>{microseconds{utc}})};
    if (static_cast<int>(utc_day.year()) < 1 || static_cast<int>(utc_day.year()) > 9999) {
        result.problem = TimestampProblem::out_of_range;
        return result;
    }
    result.value = NormalizedTimestamp{format_utc_micros(utc), utc};
    return result;
}

}  // namespace bundle::persistence
