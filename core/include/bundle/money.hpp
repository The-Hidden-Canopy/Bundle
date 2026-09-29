#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace bundle {

class CurrencyCode final {
public:
    static std::optional<CurrencyCode> parse(std::string_view value);

    [[nodiscard]] const std::string& value() const noexcept { return value_; }

    friend bool operator==(const CurrencyCode&, const CurrencyCode&) = default;

private:
    explicit CurrencyCode(std::string value) : value_(std::move(value)) {}

    std::string value_;
};

struct Money final {
    std::int64_t minor_units;
    CurrencyCode currency;

    friend bool operator==(const Money&, const Money&) = default;
};

enum class MoneyError {
    currency_mismatch,
    overflow,
};

using MoneyResult = std::variant<Money, MoneyError>;

[[nodiscard]] MoneyResult add(const Money& left, const Money& right) noexcept;
[[nodiscard]] MoneyResult subtract(const Money& left, const Money& right) noexcept;

// Currency precision is metadata, not a universal "cents" assumption.
[[nodiscard]] std::optional<std::uint8_t> currency_exponent(
    const CurrencyCode& currency) noexcept;

}  // namespace bundle
