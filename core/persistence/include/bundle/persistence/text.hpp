#pragma once

// Hashing and timestamp text with the Python bridge's exact conventions.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace bundle::persistence {

// Lowercase hex SHA-256 of raw bytes (hashlib.sha256(...).hexdigest()).
[[nodiscard]] std::string sha256_hex(std::string_view bytes);

// "sha256:" + hex of the bytes.
[[nodiscard]] std::string sha256_tagged(std::string_view bytes);

// The bridge's digest check: "sha256:" + 64 hex digits, either case.
[[nodiscard]] bool is_sha256_digest(std::string_view value) noexcept;

struct NormalizedTimestamp final {
    std::string text;          // UTC, Python datetime.isoformat() layout, "+00:00" -> "Z"
    std::int64_t unix_micros;  // for ordering and conversion to the domain model
};

enum class TimestampProblem {
    invalid,   // not in the supported RFC 3339 / ISO 8601 subset
    naive,     // parsed but carries no UTC offset
    out_of_range,
};

struct TimestampParse final {
    std::optional<NormalizedTimestamp> value;
    TimestampProblem problem = TimestampProblem::invalid;
};

// Parses the subset of datetime.fromisoformat() the native adapter supports and
// normalizes it exactly as the Python bridge does (astimezone(UTC).isoformat()).
// Supported: YYYY-MM-DD or YYYYMMDD; 'T' or ' ' separator; HH[:MM[:SS]] or
// HHMM[SS] with optional '.'/',' fraction (truncated to microseconds); offset
// 'Z', +HH, +HHMM, +HH:MM, +HH:MM:SS[.ffffff] (optionally preceded by one space).
// ISO week/ordinal dates are rejected rather than reinterpreted.
[[nodiscard]] TimestampParse parse_timestamp(std::string_view value);

// Formats microseconds since the epoch as the bridge's UTC text.
[[nodiscard]] std::string format_utc_micros(std::int64_t unix_micros);

}  // namespace bundle::persistence
