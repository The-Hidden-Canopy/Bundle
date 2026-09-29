#include "bundle/budgets.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>

namespace bundle {
namespace {

std::optional<UtcTimestamp> transaction_date(const Transaction& transaction) {
    if (transaction.posted_at.has_value()) {
        return transaction.posted_at;
    }
    if (transaction.authorized_at.has_value()) {
        return transaction.authorized_at;
    }
    return std::nullopt;
}

std::optional<Money> add_money(const Money& left, const Money& right) {
    const auto result = add(left, right);
    if (!std::holds_alternative<Money>(result)) {
        return std::nullopt;
    }
    return std::get<Money>(result);
}

}  // namespace

BudgetProjectionResult project_budget_allocations(
    const Budget& budget,
    const std::vector<Transaction>& transactions,
    const UtcTimestamp period_start,
    const UtcTimestamp period_end) noexcept {
    if (period_end < period_start) {
        return BudgetError::invalid_period;
    }

    std::vector<BudgetAllocation> projected;
    projected.reserve(budget.allocations.size());
    for (const auto& allocation : budget.allocations) {
        Money spent{0, allocation.limit.currency};
        Money pending{0, allocation.limit.currency};
        for (const auto& transaction : transactions) {
            if (!transaction.category_id.has_value() ||
                *transaction.category_id != allocation.category_id ||
                transaction.direction != TransactionDirection::debit) {
                continue;
            }
            if (transaction.amount.currency != allocation.limit.currency) {
                return BudgetError::currency_mismatch;
            }
            const auto date = transaction_date(transaction);
            if (!date.has_value() || *date < period_start || *date > period_end ||
                transaction.transaction_state == TransactionState::removed ||
                transaction.transaction_state == TransactionState::reversed) {
                continue;
            }
            if (transaction.transaction_state == TransactionState::pending) {
                const auto result = add_money(pending, transaction.amount);
                if (!result.has_value()) {
                    return BudgetError::arithmetic_overflow;
                }
                pending = *result;
            } else if (transaction.transaction_state == TransactionState::posted) {
                const auto result = add_money(spent, transaction.amount);
                if (!result.has_value()) {
                    return BudgetError::arithmetic_overflow;
                }
                spent = *result;
            }
        }
        const auto spent_and_pending = add_money(spent, pending);
        if (!spent_and_pending.has_value()) {
            return BudgetError::arithmetic_overflow;
        }
        const auto remaining = subtract(allocation.limit, *spent_and_pending);
        if (!std::holds_alternative<Money>(remaining)) {
            return BudgetError::arithmetic_overflow;
        }
        std::int64_t burn_rate = 0;
        if (allocation.limit.minor_units > 0) {
            const auto maximum = std::numeric_limits<std::int64_t>::max();
            if (spent.minor_units > maximum / 10'000) {
                burn_rate = maximum;
            } else {
                burn_rate = (spent.minor_units * 10'000) / allocation.limit.minor_units;
            }
        }
        projected.push_back(BudgetAllocation{
            .category_id = allocation.category_id,
            .limit = allocation.limit,
            .spent = spent,
            .pending = pending,
            .remaining = std::get<Money>(remaining),
            .burn_rate_basis_points = burn_rate,
        });
    }
    return projected;
}

}  // namespace bundle
