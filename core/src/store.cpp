#include "bundle/store.hpp"

#include <algorithm>
#include <cctype>
#include <string_view>
#include <utility>

namespace bundle {
namespace {

class JsonParser final {
public:
    explicit JsonParser(const std::string_view value) : value_(value) {}

    [[nodiscard]] bool parse_object_document() {
        skip_space();
        if (!parse_object(0)) {
            return false;
        }
        skip_space();
        return position_ == value_.size();
    }

private:
    void skip_space() {
        while (position_ < value_.size() &&
               std::isspace(static_cast<unsigned char>(value_[position_])) != 0) {
            ++position_;
        }
    }

    bool parse_object(const std::size_t depth) {
        if (depth > 64 || !consume('{')) {
            return false;
        }
        skip_space();
        if (consume('}')) {
            return true;
        }
        for (;;) {
            skip_space();
            if (!parse_string()) {
                return false;
            }
            skip_space();
            if (!consume(':')) {
                return false;
            }
            if (!parse_value(depth + 1)) {
                return false;
            }
            skip_space();
            if (consume('}')) {
                return true;
            }
            if (!consume(',')) {
                return false;
            }
        }
    }

    bool parse_array(const std::size_t depth) {
        if (depth > 64 || !consume('[')) {
            return false;
        }
        skip_space();
        if (consume(']')) {
            return true;
        }
        for (;;) {
            if (!parse_value(depth + 1)) {
                return false;
            }
            skip_space();
            if (consume(']')) {
                return true;
            }
            if (!consume(',')) {
                return false;
            }
        }
    }

    bool parse_value(const std::size_t depth) {
        skip_space();
        if (position_ >= value_.size()) {
            return false;
        }
        switch (value_[position_]) {
            case '{':
                return parse_object(depth);
            case '[':
                return parse_array(depth);
            case '"':
                return parse_string();
            case 't':
                return consume_literal("true");
            case 'f':
                return consume_literal("false");
            case 'n':
                return consume_literal("null");
            default:
                return parse_number();
        }
    }

    bool parse_string() {
        if (!consume('"')) {
            return false;
        }
        while (position_ < value_.size()) {
            const auto character = static_cast<unsigned char>(value_[position_++]);
            if (character == '"') {
                return true;
            }
            if (character < 0x20) {
                return false;
            }
            if (character == '\\') {
                if (position_ >= value_.size()) {
                    return false;
                }
                const auto escaped = value_[position_++];
                if (escaped == 'u') {
                    if (position_ + 4 > value_.size()) {
                        return false;
                    }
                    for (std::size_t index = 0; index < 4; ++index) {
                        if (std::isxdigit(static_cast<unsigned char>(value_[position_++])) == 0) {
                            return false;
                        }
                    }
                } else if (escaped != '"' && escaped != '\\' && escaped != '/' &&
                           escaped != 'b' && escaped != 'f' && escaped != 'n' &&
                           escaped != 'r' && escaped != 't') {
                    return false;
                }
            }
        }
        return false;
    }

    bool parse_number() {
        const auto start = position_;
        consume('-');
        if (consume('0')) {
            if (position_ < value_.size() &&
                std::isdigit(static_cast<unsigned char>(value_[position_])) != 0) {
                return false;
            }
        } else {
            if (position_ >= value_.size() ||
                std::isdigit(static_cast<unsigned char>(value_[position_])) == 0 ||
                value_[position_] == '0') {
                position_ = start;
                return false;
            }
            while (position_ < value_.size() &&
                   std::isdigit(static_cast<unsigned char>(value_[position_])) != 0) {
                ++position_;
            }
        }
        if (consume('.')) {
            const auto fraction_start = position_;
            while (position_ < value_.size() &&
                   std::isdigit(static_cast<unsigned char>(value_[position_])) != 0) {
                ++position_;
            }
            if (position_ == fraction_start) {
                return false;
            }
        }
        if (position_ < value_.size() && (value_[position_] == 'e' || value_[position_] == 'E')) {
            ++position_;
            consume('+');
            consume('-');
            const auto exponent_start = position_;
            while (position_ < value_.size() &&
                   std::isdigit(static_cast<unsigned char>(value_[position_])) != 0) {
                ++position_;
            }
            if (position_ == exponent_start) {
                return false;
            }
        }
        return position_ > start;
    }

    bool consume(const char expected) {
        if (position_ < value_.size() && value_[position_] == expected) {
            ++position_;
            return true;
        }
        return false;
    }

    bool consume_literal(const std::string_view literal) {
        if (value_.substr(position_, literal.size()) != literal) {
            return false;
        }
        position_ += literal.size();
        return true;
    }

    std::string_view value_;
    std::size_t position_ = 0;
};

bool valid_sha256(const std::string& value) {
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

}  // namespace

StoreResult FinanceStore::append_source(const SourceRef& source_ref) {
    if (validate_source_ref(source_ref).has_value()) {
        return StoreError::invalid_source;
    }
    const auto duplicate = std::find_if(
        sources_.begin(), sources_.end(), [&source_ref](const SourceRef& existing) {
            return existing.source_id == source_ref.source_id &&
                   existing.source_version == source_ref.source_version;
        });
    if (duplicate != sources_.end()) {
        return *duplicate == source_ref
                   ? StoreError::duplicate_object
                   : StoreError::revision_conflict;
    }
    sources_.push_back(source_ref);
    append_event(
        "source.appended",
        source_ref.source_id,
        source_ref.imported_at,
        source_ref.content_hash);
    return std::monostate{};
}

StoreResult FinanceStore::append_account(const Account& account) {
    if (validate_account(account).has_value()) {
        return StoreError::invalid_account;
    }
    const auto duplicate = std::find_if(
        accounts_.begin(), accounts_.end(), [&account](const Account& existing) {
            return existing.account_id == account.account_id;
        });
    if (duplicate != accounts_.end()) {
        return *duplicate == account ? StoreError::duplicate_object
                                     : StoreError::revision_conflict;
    }
    const auto source = std::find_if(
        sources_.begin(), sources_.end(), [&account](const SourceRef& existing) {
            return existing.source_id == account.source_id;
        });
    if (source == sources_.end()) {
        return StoreError::missing_source;
    }
    accounts_.push_back(account);
    append_event(
        "account.appended",
        account.account_id,
        account.balance_observed_at,
        source->content_hash);
    return std::monostate{};
}

StoreResult FinanceStore::append_balance_observation(
    const BalanceObservation& observation) {
    if (validate_balance_observation(observation).has_value()) {
        return StoreError::invalid_balance_observation;
    }
    const auto account = std::find_if(
        accounts_.begin(), accounts_.end(), [&observation](const Account& existing) {
            return existing.account_id == observation.account_id;
        });
    if (account == accounts_.end()) {
        return StoreError::missing_account;
    }
    const auto source = std::find_if(
        sources_.begin(), sources_.end(), [&observation](const SourceRef& existing) {
            return existing.source_id == observation.source_ref.source_id &&
                   existing.source_version == observation.source_ref.source_version;
        });
    if (source == sources_.end()) {
        return StoreError::missing_source;
    }
    if (*source != observation.source_ref) {
        return StoreError::revision_conflict;
    }
    const auto duplicate = std::find_if(
        balance_observations_.begin(), balance_observations_.end(),
        [&observation](const BalanceObservation& existing) {
            return existing.observation_id == observation.observation_id;
        });
    if (duplicate != balance_observations_.end()) {
        return *duplicate == observation ? StoreError::duplicate_object
                                         : StoreError::revision_conflict;
    }
    balance_observations_.push_back(observation);
    append_event(
        "balance_observation.appended",
        observation.observation_id,
        observation.recorded_at,
        source->content_hash);
    return std::monostate{};
}

StoreResult FinanceStore::append_transaction(const Transaction& transaction) {
    if (validate_transaction(transaction).has_value()) {
        return StoreError::invalid_transaction;
    }
    const auto account = std::find_if(
        accounts_.begin(), accounts_.end(), [&transaction](const Account& existing) {
            return existing.account_id == transaction.account_id;
        });
    if (account == accounts_.end()) {
        return StoreError::missing_account;
    }
    const auto source = std::find_if(
        sources_.begin(), sources_.end(), [&transaction](const SourceRef& existing) {
            return existing.source_id == transaction.source_ref.source_id &&
                   existing.source_version == transaction.source_ref.source_version;
        });
    if (source == sources_.end()) {
        return StoreError::missing_source;
    }
    if (*source != transaction.source_ref) {
        return StoreError::revision_conflict;
    }
    if (transaction.amount.currency != account->currency) {
        return StoreError::currency_mismatch;
    }

    const auto same_transaction_id = std::find_if(
        transactions_.begin(), transactions_.end(), [&transaction](const Transaction& existing) {
            return existing.transaction_id == transaction.transaction_id;
        });
    if (same_transaction_id != transactions_.end()) {
        return *same_transaction_id == transaction ? StoreError::duplicate_object
                                                   : StoreError::revision_conflict;
    }

    const auto same_revision = std::find_if(
        transactions_.begin(), transactions_.end(), [&transaction](const Transaction& existing) {
            return existing.account_id == transaction.account_id &&
                   existing.source_transaction_id == transaction.source_transaction_id &&
                   existing.revision == transaction.revision;
        });
    if (same_revision != transactions_.end()) {
        return *same_revision == transaction ? StoreError::duplicate_object
                                             : StoreError::revision_conflict;
    }

    const auto latest = latest_transaction(
        transaction.account_id, transaction.source_transaction_id);
    if (latest.has_value()) {
        if (transaction.revision != latest->revision + 1 ||
            !transaction.supersedes_transaction_revision.has_value() ||
            *transaction.supersedes_transaction_revision != latest->revision) {
            return StoreError::revision_gap;
        }
    } else if (transaction.revision != 1 || transaction.supersedes_transaction_revision.has_value()) {
        return StoreError::revision_gap;
    }

    transactions_.push_back(transaction);
    append_event(
        "transaction_revision.appended",
        transaction.transaction_id,
        transaction.observed_at,
        source->content_hash);
    return std::monostate{};
}

StoreResult FinanceStore::append_calculation_receipt(
    const CalculationReceipt& receipt) {
    if (receipt.receipt_id.empty() || receipt.engine.empty() ||
        receipt.engine_version.empty() || receipt.policy_id.empty() ||
        receipt.policy_version.empty() || receipt.outputs_json.empty() ||
        !valid_sha256(receipt.input_hash) ||
        !JsonParser(receipt.outputs_json).parse_object_document()) {
        return StoreError::invalid_calculation_receipt;
    }
    const auto existing = std::find_if(
        calculation_receipts_.begin(), calculation_receipts_.end(),
        [&receipt](const CalculationReceipt& value) {
            return value.receipt_id == receipt.receipt_id;
        });
    if (existing != calculation_receipts_.end()) {
        return *existing == receipt ? StoreError::duplicate_object
                                    : StoreError::revision_conflict;
    }
    calculation_receipts_.push_back(receipt);
    append_event(
        "calculation_receipt.appended",
        receipt.receipt_id,
        receipt.calculated_at,
        receipt.input_hash);
    return std::monostate{};
}

std::optional<BalanceObservation> FinanceStore::latest_balance(
    const std::string& account_id) const {
    std::optional<BalanceObservation> latest;
    for (const auto& observation : balance_observations_) {
        if (observation.account_id != account_id ||
            (observation.status != BalanceObservationStatus::valid &&
             observation.status != BalanceObservationStatus::stale)) {
            continue;
        }
        if (!latest.has_value() || observation.recorded_at > latest->recorded_at) {
            latest = observation;
        }
    }
    return latest;
}

std::optional<Transaction> FinanceStore::latest_transaction(
    const std::string& account_id,
    const std::string& source_transaction_id) const {
    std::optional<Transaction> latest;
    for (const auto& transaction : transactions_) {
        if (transaction.account_id != account_id ||
            transaction.source_transaction_id != source_transaction_id) {
            continue;
        }
        if (!latest.has_value() || transaction.revision > latest->revision) {
            latest = transaction;
        }
    }
    return latest;
}

bool FinanceStore::has_account(const std::string& account_id) const noexcept {
    return std::any_of(accounts_.begin(), accounts_.end(), [&account_id](const Account& account) {
        return account.account_id == account_id;
    });
}

void FinanceStore::append_event(
    std::string event_type,
    std::string object_id,
    const UtcTimestamp occurred_at,
    std::string content_hash) {
    event_log_.push_back(DomainEvent{
        .event_id = "event-" + std::to_string(event_log_.size() + 1),
        .event_type = std::move(event_type),
        .object_id = std::move(object_id),
        .occurred_at = occurred_at,
        .content_hash = std::move(content_hash),
    });
}

}  // namespace bundle
