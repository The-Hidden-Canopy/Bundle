#include "bundle/activity.hpp"

#include <cassert>
#include <optional>
#include <string>
#include <vector>

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
        .metadata = "activity-fixture",
    };
}

bundle::Transaction transaction(
    const std::string& id,
    const std::string& source_transaction_id,
    const std::string& account_id,
    const std::int64_t amount,
    const std::int64_t day,
    const std::string& merchant,
    const bundle::TransactionState state = bundle::TransactionState::posted,
    const std::uint64_t revision = 1,
    const std::optional<std::string>& category = std::nullopt,
    const std::optional<std::string>& recurring = std::nullopt) {
    const auto at = bundle::UtcTimestamp::from_unix_millis(day * 86'400'000LL);
    return bundle::Transaction{
        .transaction_id = id,
        .account_id = account_id,
        .source_transaction_id = source_transaction_id,
        .revision = revision,
        .amount = bundle::Money{amount, usd()},
        .direction = bundle::TransactionDirection::debit,
        .transaction_state = state,
        .merchant_raw = merchant,
        .merchant_id = std::nullopt,
        .description = "Activity fixture",
        .category_id = category,
        .authorized_at = at,
        .posted_at = at,
        .observed_at = at,
        .recurring_candidate_id = recurring,
        .source_ref = source("source-" + id),
        .supersedes_transaction_revision =
            revision > 1 ? std::optional<std::uint64_t>{revision - 1} : std::nullopt,
        .user_note = "",
        .user_tags = {},
    };
}

void normalization_is_conservative_and_deterministic() {
    assert(bundle::normalize_merchant_descriptor(
               "ADOBE *CREATIVE CLD 4085366000 CA") == "Adobe Creative Cloud");
    assert(bundle::normalize_merchant_descriptor("   1234 *** ").empty());
}

void user_correction_outranks_inference_and_conflicts_fail_closed() {
    const auto fixture = transaction(
        "tx-1", "source-tx-1", "checking", 1'000, 1, "ADOBE *CREATIVE CLD 4085366000 CA");
    const auto resolved = bundle::resolve_merchant(
        fixture,
        {
            bundle::MerchantAlias{
                .merchant_id = "adobe",
                .alias = "ADOBE *CREATIVE CLD",
                .canonical_name = "Adobe Creative Cloud",
                .confidence = bundle::Confidence::verified,
                .user_confirmed = true,
            },
            bundle::MerchantAlias{
                .merchant_id = "creative-cloud-inferred",
                .alias = "ADOBE *CREATIVE CLD",
                .canonical_name = "Creative Cloud",
                .confidence = bundle::Confidence::inferred,
                .user_confirmed = false,
            },
        });
    assert(resolved.merchant_id.has_value());
    assert(*resolved.merchant_id == "adobe");
    assert(resolved.user_confirmed);

    const auto conflict = bundle::resolve_merchant(
        fixture,
        {
            bundle::MerchantAlias{
                .merchant_id = "merchant-a",
                .alias = "ADOBE *CREATIVE CLD",
                .canonical_name = "Merchant A",
                .confidence = bundle::Confidence::verified,
                .user_confirmed = true,
            },
            bundle::MerchantAlias{
                .merchant_id = "merchant-b",
                .alias = "ADOBE *CREATIVE CLD",
                .canonical_name = "Merchant B",
                .confidence = bundle::Confidence::verified,
                .user_confirmed = true,
            },
        });
    assert(!conflict.merchant_id.has_value());
    assert(conflict.confidence == bundle::Confidence::unknown);
}

void activity_uses_latest_revision_and_read_only_filters() {
    const auto old_revision = transaction(
        "tx-1-r1", "source-tx-1", "checking", 1'000, 1, "OLD MERCHANT",
        bundle::TransactionState::posted, 1, std::string{"dining"});
    const auto latest_revision = transaction(
        "tx-1-r2", "source-tx-1", "checking", 1'100, 2, "ADOBE *CREATIVE CLD",
        bundle::TransactionState::posted, 2, std::string{"dining"}, std::string{"recurring-1"});
    const auto reversed = transaction(
        "tx-2", "source-tx-2", "checking", 2'000, 3, "REVERSED",
        bundle::TransactionState::reversed, 1, std::string{"dining"});
    const auto removed = transaction(
        "tx-3", "source-tx-3", "checking", 3'000, 4, "REMOVED",
        bundle::TransactionState::removed, 1, std::string{"dining"});
    const auto other_account = transaction(
        "tx-4", "source-tx-4", "savings", 4'000, 5, "SAVINGS",
        bundle::TransactionState::posted, 1, std::string{"dining"});

    const auto result = bundle::build_activity(
        {old_revision, latest_revision, reversed, removed, other_account},
        {
            bundle::MerchantAlias{
                .merchant_id = "adobe",
                .alias = "ADOBE *CREATIVE CLD",
                .canonical_name = "Adobe Creative Cloud",
                .confidence = bundle::Confidence::inferred,
                .user_confirmed = false,
            },
        },
        bundle::ActivityFilter{
            .account_id = std::string{"checking"},
            .from = std::nullopt,
            .through = std::nullopt,
            .search = "creative",
            .merchant_id = std::string{"adobe"},
            .category_id = std::string{"dining"},
            .state = std::nullopt,
            .include_removed = false,
            .include_reversed = false,
        });
    assert(result.size() == 1);
    assert(result[0].transaction.transaction_id == "tx-1-r2");
    assert(result[0].transaction.amount.minor_units == 1'100);
    assert(result[0].recurring);
    assert(result[0].merchant.display_name == "Adobe Creative Cloud");
}

}  // namespace

int main() {
    normalization_is_conservative_and_deterministic();
    user_correction_outranks_inference_and_conflicts_fail_closed();
    activity_uses_latest_revision_and_read_only_filters();
    return 0;
}
