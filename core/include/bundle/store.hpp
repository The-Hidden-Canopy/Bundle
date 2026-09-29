#pragma once

#include "bundle/financial.hpp"

#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace bundle {

struct DomainEvent final {
    std::string event_id;
    std::string event_type;
    std::string object_id;
    UtcTimestamp occurred_at;
    std::string content_hash;
};

struct CalculationReceipt final {
    std::string receipt_id;
    std::string engine;
    std::string engine_version;
    UtcTimestamp calculated_at;
    std::vector<std::string> input_refs;
    std::string input_hash;
    std::string policy_id;
    std::string policy_version;
    std::string outputs_json;

    friend bool operator==(const CalculationReceipt&, const CalculationReceipt&) = default;
};

enum class StoreError {
    invalid_source,
    invalid_account,
    invalid_balance_observation,
    invalid_transaction,
    invalid_calculation_receipt,
    missing_source,
    missing_account,
    duplicate_object,
    revision_conflict,
    revision_gap,
    currency_mismatch,
};

using StoreResult = std::variant<std::monostate, StoreError>;

class FinanceStore final {
public:
    [[nodiscard]] StoreResult append_source(const SourceRef& source_ref);
    [[nodiscard]] StoreResult append_account(const Account& account);
    [[nodiscard]] StoreResult append_balance_observation(
        const BalanceObservation& observation);
    [[nodiscard]] StoreResult append_transaction(const Transaction& transaction);
    [[nodiscard]] StoreResult append_calculation_receipt(
        const CalculationReceipt& receipt);

    [[nodiscard]] const std::vector<SourceRef>& sources() const noexcept {
        return sources_;
    }
    [[nodiscard]] const std::vector<Account>& accounts() const noexcept {
        return accounts_;
    }
    [[nodiscard]] const std::vector<BalanceObservation>& balance_observations() const noexcept {
        return balance_observations_;
    }
    [[nodiscard]] const std::vector<Transaction>& transactions() const noexcept {
        return transactions_;
    }
    [[nodiscard]] const std::vector<DomainEvent>& event_log() const noexcept {
        return event_log_;
    }
    [[nodiscard]] const std::vector<CalculationReceipt>& calculation_receipts() const noexcept {
        return calculation_receipts_;
    }

    [[nodiscard]] std::optional<BalanceObservation> latest_balance(
        const std::string& account_id) const;
    [[nodiscard]] std::optional<Transaction> latest_transaction(
        const std::string& account_id,
        const std::string& source_transaction_id) const;

private:
    [[nodiscard]] bool has_account(const std::string& account_id) const noexcept;
    void append_event(
        std::string event_type,
        std::string object_id,
        UtcTimestamp occurred_at,
        std::string content_hash);

    std::vector<SourceRef> sources_;
    std::vector<Account> accounts_;
    std::vector<BalanceObservation> balance_observations_;
    std::vector<Transaction> transactions_;
    std::vector<CalculationReceipt> calculation_receipts_;
    std::vector<DomainEvent> event_log_;
};

}  // namespace bundle
