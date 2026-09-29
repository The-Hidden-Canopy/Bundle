#include "bundle/commitments.hpp"

#include <cassert>
#include <string>

namespace {

bundle::CurrencyCode usd() {
    return *bundle::CurrencyCode::parse("USD");
}

bundle::SourceRef source(const std::string& id) {
    return bundle::SourceRef{
        .source_id = id,
        .source_kind = bundle::SourceKind::manual,
        .provider_id = std::nullopt,
        .external_object_id = std::nullopt,
        .imported_at = bundle::UtcTimestamp::from_unix_millis(1),
        .observed_at = bundle::UtcTimestamp::from_unix_millis(1),
        .content_hash = "sha256:" + std::string(64, '0'),
        .source_version = 1,
        .metadata = "fixture",
    };
}

bundle::Transaction transaction(
    const std::string& id,
    const std::int64_t amount,
    const std::int64_t day,
    const bundle::TransactionState state = bundle::TransactionState::posted,
    const bundle::TransactionDirection direction = bundle::TransactionDirection::debit) {
    const auto at = bundle::UtcTimestamp::from_unix_millis(day * 86'400'000);
    return bundle::Transaction{
        .transaction_id = id,
        .account_id = "checking",
        .source_transaction_id = id,
        .revision = 1,
        .amount = bundle::Money{amount, usd()},
        .direction = direction,
        .transaction_state = state,
        .merchant_raw = "Example Streaming",
        .merchant_id = std::nullopt,
        .description = "Recurring example",
        .category_id = std::nullopt,
        .authorized_at = at,
        .posted_at = at,
        .observed_at = at,
        .recurring_candidate_id = std::nullopt,
        .source_ref = source(id),
        .supersedes_transaction_revision = std::nullopt,
        .user_note = "",
        .user_tags = {},
    };
}

void repeated_posted_debits_become_candidates_only() {
    const std::vector<bundle::Transaction> transactions{
        transaction("t-1", 2200, 0),
        transaction("t-2", 2200, 30),
        transaction("t-3", 2200, 60),
        transaction("credit", 2200, 90, bundle::TransactionState::posted,
                    bundle::TransactionDirection::credit),
        transaction("pending", 2200, 90, bundle::TransactionState::pending),
    };
    const auto candidates = bundle::detect_recurring_candidates(transactions);
    assert(candidates.size() == 1);
    assert(candidates[0].status == bundle::CommitmentStatus::candidate);
    assert(candidates[0].confidence == bundle::Confidence::inferred);
    assert(!candidates[0].user_confirmed);
    assert(candidates[0].amount_model.kind == bundle::AmountModelKind::fixed);
    assert(candidates[0].related_transaction_ids.size() == 3);
    assert(candidates[0].next_expected_at.unix_millis == 90LL * 86'400'000);
}

void variable_amounts_remain_ranges_and_weak_evidence_is_ignored() {
    auto first = transaction("t-1", 1000, 0);
    auto second = transaction("t-2", 1500, 30);
    second.merchant_raw = "Example Streaming";
    const auto candidates = bundle::detect_recurring_candidates({first, second});
    assert(candidates.size() == 1);
    assert(candidates[0].amount_model.kind == bundle::AmountModelKind::range);
    assert(candidates[0].amount_model.minimum->minor_units == 1000);
    assert(candidates[0].amount_model.maximum->minor_units == 1500);

    auto without_posted_time = transaction("single", 1000, 30);
    without_posted_time.posted_at = std::nullopt;
    without_posted_time.authorized_at = std::nullopt;
    assert(bundle::detect_recurring_candidates({without_posted_time}).empty());
}

}  // namespace

int main() {
    repeated_posted_debits_become_candidates_only();
    variable_amounts_remain_ranges_and_weak_evidence_is_ignored();
    return 0;
}
