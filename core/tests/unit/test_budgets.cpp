#include "bundle/budgets.hpp"

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
    const bundle::TransactionState state,
    const bundle::TransactionDirection direction = bundle::TransactionDirection::debit) {
    const auto at = bundle::UtcTimestamp::from_unix_millis(day * 86'400'000LL);
    return bundle::Transaction{
        .transaction_id = id,
        .account_id = "checking",
        .source_transaction_id = id,
        .revision = 1,
        .amount = bundle::Money{amount, usd()},
        .direction = direction,
        .transaction_state = state,
        .merchant_raw = "Example",
        .merchant_id = std::nullopt,
        .description = "Budget fixture",
        .category_id = std::string{"dining"},
        .authorized_at = at,
        .posted_at = state == bundle::TransactionState::pending ? std::nullopt
                                                                 : std::optional<bundle::UtcTimestamp>{at},
        .observed_at = at,
        .recurring_candidate_id = std::nullopt,
        .source_ref = source(id),
        .supersedes_transaction_revision = std::nullopt,
        .user_note = "",
        .user_tags = {},
    };
}

void spent_and_pending_are_derived_from_transactions() {
    const auto budget = bundle::Budget{
        .budget_id = "monthly",
        .name = "Monthly",
        .mode = bundle::BudgetMode::category_caps,
        .allocations = {bundle::BudgetAllocation{
            .category_id = "dining",
            .limit = bundle::Money{1'000, usd()},
            .spent = bundle::Money{999, usd()},
            .pending = bundle::Money{999, usd()},
            .remaining = bundle::Money{999, usd()},
            .burn_rate_basis_points = 0,
        }},
        .period_rule = "monthly",
        .rollover_policy = "none",
        .starts_at = bundle::UtcTimestamp::from_unix_millis(0),
        .ends_at = bundle::UtcTimestamp::from_unix_millis(3 * 86'400'000LL),
        .status = "active",
    };
    const auto result = bundle::project_budget_allocations(
        budget,
        {
            transaction("posted", 250, 1, bundle::TransactionState::posted),
            transaction("pending", 100, 2, bundle::TransactionState::pending),
            transaction("credit", 900, 2, bundle::TransactionState::posted,
                        bundle::TransactionDirection::credit),
        },
        bundle::UtcTimestamp::from_unix_millis(0),
        bundle::UtcTimestamp::from_unix_millis(3 * 86'400'000LL));
    assert(std::holds_alternative<std::vector<bundle::BudgetAllocation>>(result));
    const auto& allocation = std::get<std::vector<bundle::BudgetAllocation>>(result)[0];
    assert(allocation.spent.minor_units == 250);
    assert(allocation.pending.minor_units == 100);
    assert(allocation.remaining.minor_units == 650);
    assert(allocation.burn_rate_basis_points == 2'500);
}

void invalid_period_and_currency_drift_fail_closed() {
    auto budget = bundle::Budget{
        .budget_id = "monthly",
        .name = "Monthly",
        .mode = bundle::BudgetMode::monthly_target,
        .allocations = {bundle::BudgetAllocation{
            .category_id = "dining",
            .limit = bundle::Money{1'000, usd()},
            .spent = bundle::Money{0, usd()},
            .pending = bundle::Money{0, usd()},
            .remaining = bundle::Money{1'000, usd()},
            .burn_rate_basis_points = 0,
        }},
        .period_rule = "monthly",
        .rollover_policy = "none",
        .starts_at = bundle::UtcTimestamp::from_unix_millis(0),
        .ends_at = std::nullopt,
        .status = "active",
    };
    const auto invalid = bundle::project_budget_allocations(
        budget, {}, bundle::UtcTimestamp::from_unix_millis(2),
        bundle::UtcTimestamp::from_unix_millis(1));
    assert(std::get<bundle::BudgetError>(invalid) == bundle::BudgetError::invalid_period);

    auto foreign = transaction("foreign", 1, 1, bundle::TransactionState::posted);
    foreign.amount.currency = *bundle::CurrencyCode::parse("JPY");
    const auto mismatch = bundle::project_budget_allocations(
        budget, {foreign}, bundle::UtcTimestamp::from_unix_millis(0),
        bundle::UtcTimestamp::from_unix_millis(2 * 86'400'000LL));
    assert(std::get<bundle::BudgetError>(mismatch) == bundle::BudgetError::currency_mismatch);
}

}  // namespace

int main() {
    spent_and_pending_are_derived_from_transactions();
    invalid_period_and_currency_drift_fail_closed();
    return 0;
}
