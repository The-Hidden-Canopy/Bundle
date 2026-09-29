#include "bundle/reconciliation.hpp"

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
    const std::string& account_id,
    const std::int64_t amount,
    const std::int64_t day,
    const bundle::TransactionDirection direction) {
    const auto at = bundle::UtcTimestamp::from_unix_millis(day * 86'400'000LL);
    return bundle::Transaction{
        .transaction_id = id,
        .account_id = account_id,
        .source_transaction_id = id,
        .revision = 1,
        .amount = bundle::Money{amount, usd()},
        .direction = direction,
        .transaction_state = bundle::TransactionState::posted,
        .merchant_raw = direction == bundle::TransactionDirection::debit ? "Transfer" : "Transfer",
        .merchant_id = std::nullopt,
        .description = "Reconciliation fixture",
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

void transfer_matching_requires_distinct_accounts_and_preserves_truth() {
    const auto matches = bundle::recognize_transfers(
        {
            transaction("out", "checking", 500, 1, bundle::TransactionDirection::debit),
            transaction("in", "savings", 500, 1, bundle::TransactionDirection::credit),
            transaction("same-account", "checking", 500, 1, bundle::TransactionDirection::credit),
        },
        60'000);
    assert(matches.size() == 1);
    assert(matches[0].outgoing_transaction_id == "out");
    assert(matches[0].incoming_transaction_id == "in");
}

void refund_matching_links_returned_money_to_original_spend() {
    const auto matches = bundle::recognize_refunds(
        {
            transaction("purchase", "checking", 1'000, 1, bundle::TransactionDirection::debit),
            transaction("refund", "checking", 1'000, 20, bundle::TransactionDirection::credit),
            transaction("income", "checking", 2'000, 20, bundle::TransactionDirection::credit),
        },
        30);
    assert(matches.size() == 1);
    assert(matches[0].original_transaction_id == "purchase");
    assert(matches[0].refund_transaction_id == "refund");
    assert(matches[0].days_after_original == 19);
}

void weak_evidence_and_invalid_windows_fail_closed() {
    auto ordinary_credit = transaction(
        "ordinary-credit", "checking", 1'000, 2, bundle::TransactionDirection::credit);
    ordinary_credit.merchant_raw = "PAYROLL";
    auto ordinary_debit = transaction(
        "ordinary-debit", "checking", 1'000, 1, bundle::TransactionDirection::debit);
    ordinary_debit.merchant_raw = "GROCERY";
    assert(bundle::recognize_refunds({ordinary_debit, ordinary_credit}, 30).empty());

    auto cross_account_credit = transaction(
        "cross-credit", "savings", 1'000, 1, bundle::TransactionDirection::credit);
    cross_account_credit.merchant_raw = "PAYROLL";
    assert(bundle::recognize_transfers({ordinary_debit, cross_account_credit}, 30).empty());
    assert(bundle::recognize_transfers({ordinary_debit, cross_account_credit}, -1).empty());
    assert(bundle::recognize_refunds({ordinary_debit, ordinary_credit}, -1).empty());
}

}  // namespace

int main() {
    transfer_matching_requires_distinct_accounts_and_preserves_truth();
    refund_matching_links_returned_money_to_original_spend();
    weak_evidence_and_invalid_windows_fail_closed();
    return 0;
}
