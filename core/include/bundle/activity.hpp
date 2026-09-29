#pragma once

#include "bundle/financial.hpp"
#include "bundle/planning.hpp"

#include <optional>
#include <string>
#include <vector>

namespace bundle {

struct MerchantAlias final {
    std::string merchant_id;
    std::string alias;
    std::string canonical_name;
    Confidence confidence;
    bool user_confirmed;
};

struct MerchantResolution final {
    std::optional<std::string> merchant_id;
    std::string display_name;
    Confidence confidence;
    bool user_confirmed;
};

[[nodiscard]] std::string normalize_merchant_descriptor(
    const std::string& raw_descriptor);

[[nodiscard]] MerchantResolution resolve_merchant(
    const Transaction& transaction,
    const std::vector<MerchantAlias>& aliases);

struct ActivityFilter final {
    std::optional<std::string> account_id;
    std::optional<UtcTimestamp> from;
    std::optional<UtcTimestamp> through;
    std::string search;
    std::optional<std::string> merchant_id;
    std::optional<std::string> category_id;
    std::optional<TransactionState> state;
    bool include_removed = false;
    bool include_reversed = true;
};

struct ActivityItem final {
    Transaction transaction;
    MerchantResolution merchant;
    UtcTimestamp activity_at;
    bool recurring;
};

// This is a projection only. It chooses the latest revision for each source
// transaction and never mutates or reclassifies the supplied transactions.
[[nodiscard]] std::vector<ActivityItem> build_activity(
    const std::vector<Transaction>& transactions,
    const std::vector<MerchantAlias>& aliases,
    const ActivityFilter& filter);

}  // namespace bundle
