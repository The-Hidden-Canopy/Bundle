#pragma once

#include "bundle/financial.hpp"
#include "bundle/planning.hpp"

#include <vector>

namespace bundle {

// Pattern detection produces candidate commitments only. It cannot mark a
// user obligation as verified or user-confirmed.
[[nodiscard]] std::vector<Commitment> detect_recurring_candidates(
    const std::vector<Transaction>& transactions);

}  // namespace bundle
