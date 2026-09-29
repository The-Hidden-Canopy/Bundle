#pragma once

#include "bundle/financial.hpp"

#include <cstdint>
#include <vector>

namespace bundle {

struct TransferMatch final {
    std::string outgoing_transaction_id;
    std::string incoming_transaction_id;
    Money amount;
    std::int64_t time_delta_millis;
};

struct RefundMatch final {
    std::string original_transaction_id;
    std::string refund_transaction_id;
    Money amount;
    std::int64_t days_after_original;
};

// Matching produces relationships only. It never rewrites transaction truth.
[[nodiscard]] std::vector<TransferMatch> recognize_transfers(
    const std::vector<Transaction>& transactions,
    std::int64_t maximum_time_delta_millis);

[[nodiscard]] std::vector<RefundMatch> recognize_refunds(
    const std::vector<Transaction>& transactions,
    std::int64_t maximum_days_after_original);

}  // namespace bundle
