#include "bundle/activity.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <string_view>
#include <utility>

namespace bundle {
namespace {

std::string uppercase(const std::string& value) {
    std::string result;
    result.reserve(value.size());
    for (const unsigned char character : value) {
        result.push_back(static_cast<char>(std::toupper(character)));
    }
    return result;
}

bool is_numeric_token(const std::string& token) {
    return !token.empty() && std::all_of(token.begin(), token.end(), [](const char character) {
        return std::isdigit(static_cast<unsigned char>(character)) != 0;
    });
}

std::string title_case(const std::string& token) {
    std::string result;
    result.reserve(token.size());
    for (std::size_t index = 0; index < token.size(); ++index) {
        const auto character = static_cast<unsigned char>(token[index]);
        result.push_back(static_cast<char>(index == 0 ? std::toupper(character)
                                                      : std::tolower(character)));
    }
    return result;
}

std::string normalized_search_text(const std::string& value) {
    return uppercase(value);
}

int confidence_rank(const Confidence confidence) {
    switch (confidence) {
        case Confidence::verified:
            return 6;
        case Confidence::supported:
            return 5;
        case Confidence::inferred:
            return 4;
        case Confidence::estimated:
            return 3;
        case Confidence::unknown:
            return 2;
        case Confidence::stale:
            return 1;
    }
    return 0;
}

UtcTimestamp activity_time(const Transaction& transaction) {
    if (transaction.posted_at.has_value()) {
        return *transaction.posted_at;
    }
    if (transaction.authorized_at.has_value()) {
        return *transaction.authorized_at;
    }
    return transaction.observed_at;
}

bool contains_case_insensitive(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) {
        return true;
    }
    return normalized_search_text(haystack).find(normalized_search_text(needle)) !=
           std::string::npos;
}

struct AliasCandidate final {
    const MerchantAlias* alias;
};

}  // namespace

std::string normalize_merchant_descriptor(const std::string& raw_descriptor) {
    const auto upper = uppercase(raw_descriptor);
    std::vector<std::string> tokens;
    std::string token;
    bool numeric_token_seen = false;
    for (const unsigned char character : upper) {
        if (std::isalnum(character) != 0) {
            token.push_back(static_cast<char>(character));
            continue;
        }
        if (!token.empty()) {
            if (!is_numeric_token(token)) {
                if (!(numeric_token_seen && token.size() == 2)) {
                    if (token == "CLD") {
                        token = "CLOUD";
                    }
                    tokens.push_back(title_case(token));
                }
            } else {
                numeric_token_seen = true;
            }
            token.clear();
        }
    }
    if (!token.empty()) {
        if (!is_numeric_token(token)) {
            if (!(numeric_token_seen && token.size() == 2)) {
                if (token == "CLD") {
                    token = "CLOUD";
                }
                tokens.push_back(title_case(token));
            }
        }
    }

    std::string normalized;
    for (const auto& value : tokens) {
        if (!normalized.empty()) {
            normalized.push_back(' ');
        }
        normalized += value;
    }
    return normalized;
}

MerchantResolution resolve_merchant(
    const Transaction& transaction,
    const std::vector<MerchantAlias>& aliases) {
    const auto normalized_raw = normalize_merchant_descriptor(transaction.merchant_raw);
    std::vector<AliasCandidate> candidates;
    for (const auto& alias : aliases) {
        const bool matched_merchant_id =
            transaction.merchant_id.has_value() &&
            *transaction.merchant_id == alias.merchant_id;
        if (matched_merchant_id ||
            (!normalized_raw.empty() &&
             normalize_merchant_descriptor(alias.alias) == normalized_raw)) {
            candidates.push_back(AliasCandidate{
                .alias = &alias,
            });
        }
    }

    if (!candidates.empty()) {
        std::sort(candidates.begin(), candidates.end(), [](const AliasCandidate& left,
                                                           const AliasCandidate& right) {
            if (left.alias->user_confirmed != right.alias->user_confirmed) {
                return left.alias->user_confirmed > right.alias->user_confirmed;
            }
            if (confidence_rank(left.alias->confidence) !=
                confidence_rank(right.alias->confidence)) {
                return confidence_rank(left.alias->confidence) >
                       confidence_rank(right.alias->confidence);
            }
            return left.alias->merchant_id < right.alias->merchant_id;
        });
        const auto& best = *candidates.front().alias;
        const auto same_priority = [&best](const AliasCandidate& candidate) {
            return candidate.alias->user_confirmed == best.user_confirmed &&
                   candidate.alias->confidence == best.confidence;
        };
        const auto conflicting = std::any_of(
            candidates.begin() + 1,
            candidates.end(),
            [&best, &same_priority](const AliasCandidate& candidate) {
                return same_priority(candidate) &&
                       (candidate.alias->merchant_id != best.merchant_id ||
                        candidate.alias->canonical_name != best.canonical_name);
            });
        if (conflicting) {
            return MerchantResolution{
                .merchant_id = std::nullopt,
                .display_name = normalized_raw,
                .confidence = Confidence::unknown,
                .user_confirmed = false,
            };
        }
        return MerchantResolution{
            .merchant_id = best.merchant_id,
            .display_name = best.canonical_name,
            .confidence = best.confidence,
            .user_confirmed = best.user_confirmed,
        };
    }

    return MerchantResolution{
        .merchant_id = transaction.merchant_id,
        .display_name = normalized_raw,
        .confidence = transaction.merchant_id.has_value() ? Confidence::supported
                                                            : Confidence::inferred,
        .user_confirmed = false,
    };
}

std::vector<ActivityItem> build_activity(
    const std::vector<Transaction>& transactions,
    const std::vector<MerchantAlias>& aliases,
    const ActivityFilter& filter) {
    using Key = std::pair<std::string, std::string>;
    std::map<Key, const Transaction*> latest;
    for (const auto& transaction : transactions) {
        if (transaction.transaction_id.empty()) {
            continue;
        }
        const Key key{transaction.account_id, transaction.source_transaction_id};
        const auto existing = latest.find(key);
        if (existing == latest.end() ||
            existing->second->revision < transaction.revision ||
            (existing->second->revision == transaction.revision &&
             existing->second->transaction_id < transaction.transaction_id)) {
            latest[key] = &transaction;
        }
    }

    std::vector<ActivityItem> result;
    for (const auto& [key, transaction] : latest) {
        static_cast<void>(key);
        if (filter.account_id.has_value() &&
            transaction->account_id != *filter.account_id) {
            continue;
        }
        if (filter.state.has_value()) {
            if (transaction->transaction_state != *filter.state) {
                continue;
            }
        } else {
            if (transaction->transaction_state == TransactionState::removed &&
                !filter.include_removed) {
                continue;
            }
            if (transaction->transaction_state == TransactionState::reversed &&
                !filter.include_reversed) {
                continue;
            }
        }

        const auto at = activity_time(*transaction);
        if (filter.from.has_value() && at < *filter.from) {
            continue;
        }
        if (filter.through.has_value() && at > *filter.through) {
            continue;
        }
        if (filter.category_id.has_value() &&
            (!transaction->category_id.has_value() ||
             *transaction->category_id != *filter.category_id)) {
            continue;
        }

        auto merchant = resolve_merchant(*transaction, aliases);
        if (filter.merchant_id.has_value() &&
            (!merchant.merchant_id.has_value() ||
             *merchant.merchant_id != *filter.merchant_id)) {
            continue;
        }
        if (!filter.search.empty() &&
            !contains_case_insensitive(transaction->merchant_raw, filter.search) &&
            !contains_case_insensitive(transaction->description, filter.search) &&
            !contains_case_insensitive(merchant.display_name, filter.search) &&
            (!transaction->category_id.has_value() ||
             !contains_case_insensitive(*transaction->category_id, filter.search))) {
            continue;
        }

        result.push_back(ActivityItem{
            .transaction = *transaction,
            .merchant = std::move(merchant),
            .activity_at = at,
            .recurring = transaction->recurring_candidate_id.has_value(),
        });
    }

    std::sort(result.begin(), result.end(), [](const ActivityItem& left, const ActivityItem& right) {
        if (left.activity_at != right.activity_at) {
            return left.activity_at > right.activity_at;
        }
        return left.transaction.transaction_id > right.transaction.transaction_id;
    });
    return result;
}

}  // namespace bundle
