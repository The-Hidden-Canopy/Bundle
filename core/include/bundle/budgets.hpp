#pragma once

#include "bundle/planning.hpp"

#include <optional>
#include <variant>
#include <vector>

namespace bundle {

enum class BudgetError {
    invalid_period,
    currency_mismatch,
    invalid_transaction,
    arithmetic_overflow,
};

using BudgetProjectionResult = std::variant<std::vector<BudgetAllocation>, BudgetError>;

// Spent and pending are projections of transaction history. The values on a
// BudgetAllocation are not treated as a second source of truth.
[[nodiscard]] BudgetProjectionResult project_budget_allocations(
    const Budget& budget,
    const std::vector<Transaction>& transactions,
    UtcTimestamp period_start,
    UtcTimestamp period_end) noexcept;

}  // namespace bundle
