#include "bundle/reconciliation.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <set>
#include <string>

namespace bundle {
namespace {

std::optional<UtcTimestamp> transaction_time(const Transaction& transaction) {
    if (transaction.posted_at.has_value()) {
        return transaction.posted_at;
    }
    if (transaction.authorized_at.has_value()) {
        return transaction.authorized_at;
    }
    return std::nullopt;
}

bool usable(const Transaction& transaction) {
    return transaction.transaction_state == TransactionState::posted &&
           transaction_time(transaction).has_value();
}

std::string normalized_text(const Transaction& transaction) {
    const auto& raw = transaction.merchant_id.value_or(transaction.merchant_raw);
    std::string normalized;
    normalized.reserve(raw.size());
    for (const unsigned char character : raw) {
        if (std::isalnum(character)) {
            normalized.push_back(static_cast<char>(std::toupper(character)));
        }
    }
    return normalized;
}

bool transfer_evidence(const Transaction& debit, const Transaction& credit) {
    const auto debit_text = normalized_text(debit);
    const auto credit_text = normalized_text(credit);
    return debit_text.find("TRANSFER") != std::string::npos ||
           credit_text.find("TRANSFER") != std::string::npos;
}

bool refund_evidence(const Transaction& debit, const Transaction& credit) {
    if (debit.merchant_id.has_value() && credit.merchant_id.has_value()) {
        return debit.merchant_id == credit.merchant_id;
    }
    const auto debit_text = normalized_text(debit);
    const auto credit_text = normalized_text(credit);
    return !debit_text.empty() && debit_text == credit_text;
}

std::optional<std::uint64_t> absolute_delta(
    const UtcTimestamp left,
    const UtcTimestamp right) {
    if (left.unix_millis >= right.unix_millis) {
        return static_cast<std::uint64_t>(left.unix_millis) -
               static_cast<std::uint64_t>(right.unix_millis);
    }
    return static_cast<std::uint64_t>(right.unix_millis) -
           static_cast<std::uint64_t>(left.unix_millis);
}

}  // namespace

std::vector<TransferMatch> recognize_transfers(
    const std::vector<Transaction>& transactions,
    const std::int64_t maximum_time_delta_millis) {
    if (maximum_time_delta_millis < 0) {
        return {};
    }
    std::vector<const Transaction*> outgoing;
    std::vector<const Transaction*> incoming;
    for (const auto& transaction : transactions) {
        if (!usable(transaction)) {
            continue;
        }
        if (transaction.direction == TransactionDirection::debit) {
            outgoing.push_back(&transaction);
        } else if (transaction.direction == TransactionDirection::credit) {
            incoming.push_back(&transaction);
        }
    }
    std::sort(outgoing.begin(), outgoing.end(), [](const Transaction* left, const Transaction* right) {
        return left->transaction_id < right->transaction_id;
    });
    std::sort(incoming.begin(), incoming.end(), [](const Transaction* left, const Transaction* right) {
        return left->transaction_id < right->transaction_id;
    });

    std::set<std::string> used_incoming;
    std::vector<TransferMatch> matches;
    for (const auto* debit : outgoing) {
        const auto debit_time = transaction_time(*debit);
        for (const auto* credit : incoming) {
            if (used_incoming.contains(credit->transaction_id) ||
                debit->account_id == credit->account_id ||
                debit->amount != credit->amount ||
                debit->amount.currency != credit->amount.currency ||
                !transfer_evidence(*debit, *credit)) {
                continue;
            }
            const auto credit_time = transaction_time(*credit);
            const auto delta = absolute_delta(*credit_time, *debit_time);
            if (!delta.has_value() ||
                *delta > static_cast<std::uint64_t>(maximum_time_delta_millis)) {
                continue;
            }
            used_incoming.insert(credit->transaction_id);
            matches.push_back(TransferMatch{
                .outgoing_transaction_id = debit->transaction_id,
                .incoming_transaction_id = credit->transaction_id,
                .amount = debit->amount,
                .time_delta_millis = static_cast<std::int64_t>(*delta),
            });
            break;
        }
    }
    return matches;
}

std::vector<RefundMatch> recognize_refunds(
    const std::vector<Transaction>& transactions,
    const std::int64_t maximum_days_after_original) {
    if (maximum_days_after_original < 0) {
        return {};
    }
    std::vector<const Transaction*> debits;
    std::vector<const Transaction*> credits;
    for (const auto& transaction : transactions) {
        if (!usable(transaction)) {
            continue;
        }
        if (transaction.direction == TransactionDirection::debit) {
            debits.push_back(&transaction);
        } else if (transaction.direction == TransactionDirection::credit) {
            credits.push_back(&transaction);
        }
    }
    std::sort(debits.begin(), debits.end(), [](const Transaction* left, const Transaction* right) {
        return left->transaction_id < right->transaction_id;
    });
    std::sort(credits.begin(), credits.end(), [](const Transaction* left, const Transaction* right) {
        return left->transaction_id < right->transaction_id;
    });

    std::set<std::string> used_credits;
    std::vector<RefundMatch> matches;
    for (const auto* debit : debits) {
        const auto debit_time = transaction_time(*debit);
        for (const auto* credit : credits) {
            if (used_credits.contains(credit->transaction_id) ||
                debit->account_id != credit->account_id ||
                debit->amount.currency != credit->amount.currency ||
                debit->amount.minor_units < credit->amount.minor_units ||
                !refund_evidence(*debit, *credit)) {
                continue;
            }
            const auto credit_time = transaction_time(*credit);
            if (credit_time->unix_millis < debit_time->unix_millis) {
                continue;
            }
            const auto delta = static_cast<std::uint64_t>(credit_time->unix_millis) -
                               static_cast<std::uint64_t>(debit_time->unix_millis);
            const auto maximum_days = static_cast<std::uint64_t>(maximum_days_after_original);
            const auto maximum_delta =
                maximum_days > std::numeric_limits<std::uint64_t>::max() / 86'400'000ULL
                    ? std::numeric_limits<std::uint64_t>::max()
                    : maximum_days * 86'400'000ULL;
            if (delta > maximum_delta) {
                continue;
            }
            used_credits.insert(credit->transaction_id);
            matches.push_back(RefundMatch{
                .original_transaction_id = debit->transaction_id,
                .refund_transaction_id = credit->transaction_id,
                .amount = credit->amount,
                .days_after_original = static_cast<std::int64_t>(delta / 86'400'000ULL),
            });
            break;
        }
    }
    return matches;
}

}  // namespace bundle
