#pragma once

#include "bundle/money.hpp"

#include <compare>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace bundle {

// An integer epoch value makes the UTC requirement explicit and avoids a
// naive local-time representation at the domain boundary.
struct UtcTimestamp final {
    std::int64_t unix_millis;

    static constexpr UtcTimestamp from_unix_millis(const std::int64_t value) noexcept {
        return UtcTimestamp{value};
    }

    friend bool operator==(const UtcTimestamp&, const UtcTimestamp&) = default;
    friend auto operator<=>(const UtcTimestamp&, const UtcTimestamp&) = default;
};

enum class SourceKind {
    manual,
    csv,
    ofx,
    qfx,
    bank_connector,
    card_connector,
    email,
    invoice,
    receipt,
    calendar,
    local_api,
};

struct SourceRef final {
    std::string source_id;
    SourceKind source_kind;
    std::optional<std::string> provider_id;
    std::optional<std::string> external_object_id;
    UtcTimestamp imported_at;
    UtcTimestamp observed_at;
    std::string content_hash;
    std::uint64_t source_version;
    std::string metadata;

    friend bool operator==(const SourceRef&, const SourceRef&) = default;
};

enum class AccountType {
    checking,
    savings,
    cash,
    credit_card,
    prepaid,
    investment,
    loan,
    other,
};

enum class AccountStatus {
    active,
    closed,
    unavailable,
    unknown,
};

enum class BalanceFreshness {
    fresh,
    aging,
    stale,
    unavailable,
};

struct Account final {
    std::string account_id;
    std::string source_id;
    std::string display_name;
    std::string institution_name;
    AccountType account_type;
    std::string account_subtype;
    CurrencyCode currency;
    Money current_balance;
    Money available_balance;
    UtcTimestamp balance_observed_at;
    BalanceFreshness balance_freshness;
    std::optional<std::string> last_four;
    AccountStatus status;
    bool include_in_safe_to_spend;
    bool include_in_net_position;

    friend bool operator==(const Account&, const Account&) = default;
};

enum class BalanceObservationStatus {
    valid,
    stale,
    unavailable,
    unknown,
};

struct BalanceObservation final {
    std::string observation_id;
    std::string account_id;
    std::int64_t current_minor;
    std::int64_t available_minor;
    UtcTimestamp observed_at;
    UtcTimestamp recorded_at;
    SourceRef source_ref;
    BalanceObservationStatus status;

    friend bool operator==(const BalanceObservation&, const BalanceObservation&) = default;
};

enum class TransactionDirection {
    debit,
    credit,
};

enum class TransactionState {
    pending,
    posted,
    reversed,
    removed,
    unknown,
};

struct Transaction final {
    std::string transaction_id;
    std::string account_id;
    std::string source_transaction_id;
    std::uint64_t revision;
    Money amount;
    TransactionDirection direction;
    TransactionState transaction_state;
    std::string merchant_raw;
    std::optional<std::string> merchant_id;
    std::string description;
    std::optional<std::string> category_id;
    std::optional<UtcTimestamp> authorized_at;
    std::optional<UtcTimestamp> posted_at;
    UtcTimestamp observed_at;
    std::optional<std::string> recurring_candidate_id;
    SourceRef source_ref;
    std::optional<std::uint64_t> supersedes_transaction_revision;
    std::string user_note;
    std::vector<std::string> user_tags;

    friend bool operator==(const Transaction&, const Transaction&) = default;
};

enum class FinancialError {
    empty_identifier,
    invalid_source_version,
    invalid_source_reference,
    invalid_content_hash,
    currency_mismatch,
    invalid_last_four,
    invalid_timestamp_order,
    invalid_revision,
    invalid_superseded_revision,
};

[[nodiscard]] std::optional<FinancialError> validate_source_ref(
    const SourceRef& source_ref) noexcept;
[[nodiscard]] std::optional<FinancialError> validate_account(
    const Account& account) noexcept;
[[nodiscard]] std::optional<FinancialError> validate_balance_observation(
    const BalanceObservation& observation) noexcept;
[[nodiscard]] std::optional<FinancialError> validate_transaction(
    const Transaction& transaction) noexcept;

}  // namespace bundle
