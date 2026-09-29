#include "bundle/financial.hpp"

#include <cctype>
#include <string_view>

namespace bundle {
namespace {

bool empty(const std::string& value) noexcept {
    return value.empty();
}

bool valid_sha256(const std::string& value) noexcept {
    constexpr std::string_view prefix = "sha256:";
    if (value.size() != prefix.size() + 64 || value.rfind(prefix, 0) != 0) {
        return false;
    }
    for (std::size_t index = prefix.size(); index < value.size(); ++index) {
        if (std::isxdigit(static_cast<unsigned char>(value[index])) == 0) {
            return false;
        }
    }
    return true;
}

bool valid_last_four(const std::string& value) noexcept {
    if (value.size() != 4) {
        return false;
    }
    for (const unsigned char character : value) {
        if (!std::isdigit(character)) {
            return false;
        }
    }
    return true;
}

}  // namespace

std::optional<FinancialError> validate_source_ref(const SourceRef& source_ref) noexcept {
    if (empty(source_ref.source_id)) {
        return FinancialError::empty_identifier;
    }
    if (source_ref.source_version == 0) {
        return FinancialError::invalid_source_version;
    }
    if (!valid_sha256(source_ref.content_hash)) {
        return FinancialError::invalid_content_hash;
    }
    return std::nullopt;
}

std::optional<FinancialError> validate_account(const Account& account) noexcept {
    if (empty(account.account_id) || empty(account.source_id)) {
        return FinancialError::empty_identifier;
    }
    if (account.current_balance.currency != account.currency ||
        account.available_balance.currency != account.currency) {
        return FinancialError::currency_mismatch;
    }
    if (account.last_four.has_value() && !valid_last_four(*account.last_four)) {
        return FinancialError::invalid_last_four;
    }
    return std::nullopt;
}

std::optional<FinancialError> validate_balance_observation(
    const BalanceObservation& observation) noexcept {
    if (empty(observation.observation_id) || empty(observation.account_id)) {
        return FinancialError::empty_identifier;
    }
    if (validate_source_ref(observation.source_ref).has_value()) {
        return FinancialError::invalid_source_reference;
    }
    if (observation.recorded_at < observation.observed_at) {
        return FinancialError::invalid_timestamp_order;
    }
    return std::nullopt;
}

std::optional<FinancialError> validate_transaction(
    const Transaction& transaction) noexcept {
    if (empty(transaction.transaction_id) || empty(transaction.account_id) ||
        empty(transaction.source_transaction_id)) {
        return FinancialError::empty_identifier;
    }
    if (transaction.revision == 0) {
        return FinancialError::invalid_revision;
    }
    if (transaction.supersedes_transaction_revision.has_value() &&
        *transaction.supersedes_transaction_revision >= transaction.revision) {
        return FinancialError::invalid_superseded_revision;
    }
    if (validate_source_ref(transaction.source_ref).has_value()) {
        return FinancialError::invalid_source_reference;
    }
    return std::nullopt;
}

}  // namespace bundle
