#include "bundle/commitments.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <limits>
#include <map>
#include <numeric>
#include <string>
#include <utility>

namespace bundle {
namespace {

struct CandidateTransaction final {
    const Transaction* transaction;
    UtcTimestamp at;
};

std::string normalized_merchant(const Transaction& transaction) {
    std::string value = transaction.merchant_id.value_or(transaction.merchant_raw);
    std::string normalized;
    normalized.reserve(value.size());
    bool previous_space = false;
    for (const unsigned char character : value) {
        if (std::isspace(character)) {
            if (!previous_space) {
                normalized.push_back(' ');
            }
            previous_space = true;
            continue;
        }
        previous_space = false;
        if (std::isalnum(character)) {
            normalized.push_back(static_cast<char>(std::toupper(character)));
        }
    }
    while (!normalized.empty() && normalized.back() == ' ') {
        normalized.pop_back();
    }
    return normalized;
}

std::optional<UtcTimestamp> transaction_time(const Transaction& transaction) {
    if (transaction.posted_at.has_value()) {
        return transaction.posted_at;
    }
    if (transaction.authorized_at.has_value()) {
        return transaction.authorized_at;
    }
    return std::nullopt;
}

std::optional<Cadence> cadence_for_interval(const std::int64_t days) {
    if (days >= 5 && days <= 9) {
        return Cadence::weekly;
    }
    if (days >= 12 && days <= 16) {
        return Cadence::biweekly;
    }
    if (days >= 25 && days <= 35) {
        return Cadence::monthly;
    }
    if (days >= 80 && days <= 100) {
        return Cadence::quarterly;
    }
    if (days >= 160 && days <= 200) {
        return Cadence::semiannual;
    }
    if (days >= 330 && days <= 400) {
        return Cadence::annual;
    }
    return std::nullopt;
}

std::optional<Money> sum_amounts(const std::vector<CandidateTransaction>& values) {
    if (values.empty()) {
        return std::nullopt;
    }
    Money total{0, values.front().transaction->amount.currency};
    for (const auto& value : values) {
        const auto added = add(total, value.transaction->amount);
        if (!std::holds_alternative<Money>(added)) {
            return std::nullopt;
        }
        total = std::get<Money>(added);
    }
    total.minor_units /= static_cast<std::int64_t>(values.size());
    return total;
}

}  // namespace

std::vector<Commitment> detect_recurring_candidates(
    const std::vector<Transaction>& transactions) {
    std::map<std::string, std::vector<CandidateTransaction>> groups;
    for (const auto& transaction : transactions) {
        if (transaction.direction != TransactionDirection::debit ||
            transaction.transaction_state != TransactionState::posted) {
            continue;
        }
        const auto at = transaction_time(transaction);
        const auto merchant = normalized_merchant(transaction);
        if (!at.has_value() || merchant.empty()) {
            continue;
        }
        const auto key = merchant + "|" + transaction.amount.currency.value();
        groups[key].push_back(CandidateTransaction{&transaction, *at});
    }

    std::vector<Commitment> candidates;
    for (auto& [key, values] : groups) {
        if (values.size() < 2) {
            continue;
        }
        std::sort(values.begin(), values.end(), [](const CandidateTransaction& left,
                                                   const CandidateTransaction& right) {
            if (left.at == right.at) {
                return left.transaction->transaction_id < right.transaction->transaction_id;
            }
            return left.at < right.at;
        });
        std::vector<std::int64_t> intervals;
        intervals.reserve(values.size() - 1);
        for (std::size_t index = 1; index < values.size(); ++index) {
            const auto delta = values[index].at.unix_millis - values[index - 1].at.unix_millis;
            if (delta <= 0) {
                continue;
            }
            intervals.push_back(delta / 86'400'000);
        }
        if (intervals.empty()) {
            continue;
        }
        const auto median_index = intervals.size() / 2;
        std::nth_element(intervals.begin(), intervals.begin() + median_index, intervals.end());
        const auto median_days = intervals[median_index];
        const auto cadence = cadence_for_interval(median_days);
        if (!cadence.has_value()) {
            continue;
        }

        const auto average = sum_amounts(values);
        if (!average.has_value()) {
            continue;
        }
        std::int64_t minimum = values.front().transaction->amount.minor_units;
        std::int64_t maximum = minimum;
        bool fixed_amount = true;
        for (const auto& value : values) {
            minimum = std::min(minimum, value.transaction->amount.minor_units);
            maximum = std::max(maximum, value.transaction->amount.minor_units);
            fixed_amount = fixed_amount &&
                           value.transaction->amount.minor_units == values.front().transaction->amount.minor_units;
        }
        AmountModel amount_model{
            .kind = fixed_amount ? AmountModelKind::fixed : AmountModelKind::range,
            .primary = *average,
            .minimum = fixed_amount
                           ? std::nullopt
                           : std::optional<Money>{Money{minimum, average->currency}},
            .maximum = fixed_amount
                           ? std::nullopt
                           : std::optional<Money>{Money{maximum, average->currency}},
        };
        const auto last = values.back();
        const auto interval_millis = median_days * 86'400'000;
        if (last.at.unix_millis > std::numeric_limits<std::int64_t>::max() - interval_millis) {
            continue;
        }
        std::vector<SourceRef> source_refs;
        std::vector<std::string> related_ids;
        source_refs.reserve(values.size());
        related_ids.reserve(values.size());
        for (const auto& value : values) {
            source_refs.push_back(value.transaction->source_ref);
            related_ids.push_back(value.transaction->transaction_id);
        }
        candidates.push_back(Commitment{
            .commitment_id = "candidate-" + key,
            .name = last.transaction->merchant_id.value_or(last.transaction->merchant_raw),
            .merchant_id = last.transaction->merchant_id,
            .amount_model = amount_model,
            .currency = average->currency,
            .cadence = *cadence,
            .next_expected_at = UtcTimestamp::from_unix_millis(last.at.unix_millis + interval_millis),
            .grace_window_millis = interval_millis / 5,
            .commitment_type = "recurring_candidate",
            .status = CommitmentStatus::candidate,
            .confidence = Confidence::inferred,
            .source_refs = std::move(source_refs),
            .related_transaction_ids = std::move(related_ids),
            .user_confirmed = false,
            .created_at = values.front().at,
            .updated_at = values.back().at,
        });
    }
    return candidates;
}

}  // namespace bundle
