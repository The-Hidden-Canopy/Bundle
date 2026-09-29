#include "bundle/money.hpp"

#include <array>
#include <limits>

namespace bundle {
namespace {

bool valid_currency_code(std::string_view value) noexcept {
    if (value.size() != 3) {
        return false;
    }
    for (const char character : value) {
        if (character < 'A' || character > 'Z') {
            return false;
        }
    }
    return true;
}

bool add_overflows(const std::int64_t left, const std::int64_t right) noexcept {
    const auto maximum = std::numeric_limits<std::int64_t>::max();
    const auto minimum = std::numeric_limits<std::int64_t>::min();
    return (right > 0 && left > maximum - right) ||
           (right < 0 && left < minimum - right);
}

bool subtract_overflows(const std::int64_t left, const std::int64_t right) noexcept {
    const auto maximum = std::numeric_limits<std::int64_t>::max();
    const auto minimum = std::numeric_limits<std::int64_t>::min();
    return (right < 0 && left > maximum + right) ||
           (right > 0 && left < minimum + right);
}

}  // namespace

std::optional<CurrencyCode> CurrencyCode::parse(const std::string_view value) {
    if (!valid_currency_code(value)) {
        return std::nullopt;
    }
    return CurrencyCode{std::string(value)};
}

MoneyResult add(const Money& left, const Money& right) noexcept {
    if (left.currency != right.currency) {
        return MoneyError::currency_mismatch;
    }
    if (add_overflows(left.minor_units, right.minor_units)) {
        return MoneyError::overflow;
    }
    return Money{left.minor_units + right.minor_units, left.currency};
}

MoneyResult subtract(const Money& left, const Money& right) noexcept {
    if (left.currency != right.currency) {
        return MoneyError::currency_mismatch;
    }
    if (subtract_overflows(left.minor_units, right.minor_units)) {
        return MoneyError::overflow;
    }
    return Money{left.minor_units - right.minor_units, left.currency};
}

std::optional<std::uint8_t> currency_exponent(const CurrencyCode& currency) noexcept {
    if (currency.value() == "USD" || currency.value() == "EUR" ||
        currency.value() == "GBP") {
        return 2;
    }
    if (currency.value() == "JPY") {
        return 0;
    }
    if (currency.value() == "KWD") {
        return 3;
    }
    return std::nullopt;
}

}  // namespace bundle
