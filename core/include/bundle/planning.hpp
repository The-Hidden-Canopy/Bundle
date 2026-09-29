#pragma once

#include "bundle/financial.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace bundle {

enum class Confidence {
    verified,
    supported,
    inferred,
    estimated,
    unknown,
    stale,
};

enum class ConfidenceMode {
    confirmed,
    expected,
    stress,
};

enum class AmountModelKind {
    fixed,
    range,
    rolling_average,
    last_amount,
    seasonal,
    user_entered,
};

struct AmountModel final {
    AmountModelKind kind;
    Money primary;
    std::optional<Money> minimum;
    std::optional<Money> maximum;
};

enum class Cadence {
    weekly,
    biweekly,
    monthly,
    quarterly,
    semiannual,
    annual,
    custom,
};

enum class CommitmentStatus {
    candidate,
    active,
    paused,
    cancelled,
    ended,
    unknown,
};

struct Commitment final {
    std::string commitment_id;
    std::string name;
    std::optional<std::string> merchant_id;
    AmountModel amount_model;
    CurrencyCode currency;
    Cadence cadence;
    UtcTimestamp next_expected_at;
    std::int64_t grace_window_millis;
    std::string commitment_type;
    CommitmentStatus status;
    Confidence confidence;
    std::vector<SourceRef> source_refs;
    std::vector<std::string> related_transaction_ids;
    bool user_confirmed;
    UtcTimestamp created_at;
    UtcTimestamp updated_at;
};

struct IncomeStream final {
    std::string income_id;
    std::string name;
    std::string account_id;
    std::optional<std::string> payer_merchant_id;
    AmountModel amount_model;
    Cadence cadence;
    UtcTimestamp next_expected_at;
    Confidence confidence;
    std::vector<SourceRef> source_refs;
    std::vector<std::string> related_transaction_ids;
    bool include_in_forecast;
};

enum class BudgetMode {
    category_caps,
    envelope_allocation,
    fixed_flexible_split,
    monthly_target,
    no_formal_budget,
};

struct BudgetAllocation final {
    std::string category_id;
    Money limit;
    Money spent;
    Money pending;
    Money remaining;
    std::int64_t burn_rate_basis_points;
};

struct Budget final {
    std::string budget_id;
    std::string name;
    BudgetMode mode;
    std::vector<BudgetAllocation> allocations;
    std::string period_rule;
    std::string rollover_policy;
    UtcTimestamp starts_at;
    std::optional<UtcTimestamp> ends_at;
    std::string status;
};

enum class GoalStatus {
    active,
    paused,
    completed,
    cancelled,
};

struct Goal final {
    std::string goal_id;
    std::string name;
    Money target_amount;
    std::optional<UtcTimestamp> target_date;
    Money current_reserved;
    Money planned_contribution;
    std::int32_t priority;
    bool protected_goal;
    std::vector<std::string> funding_account_ids;
    GoalStatus status;
};

struct CashBuffer final {
    Money amount;
    std::string scope;
    std::string reason;
};

enum class ForecastEventKind {
    commitment,
    income,
    planned_transfer,
    budget_reserve,
    goal_contribution,
    manual_event,
    known_one_time_charge,
    known_refund,
};

enum class ForecastDirection {
    inflow,
    outflow,
};

struct ForecastEvent final {
    std::string event_id;
    ForecastEventKind kind;
    UtcTimestamp expected_at;
    AmountModel amount_model;
    ForecastDirection direction;
    Confidence confidence;
    std::string account_scope;
    std::string source_type;
    std::optional<SourceRef> source_ref;
};

struct ForecastPoint final {
    UtcTimestamp date;
    Money opening;
    Money inflows;
    Money outflows;
    Money closing;
    Money floor;
    Confidence confidence;
    std::vector<std::string> contributing_event_ids;
};

struct ForecastRequest final {
    Money initial_liquid;
    Money protected_floor;
    UtcTimestamp start;
    UtcTimestamp horizon_end;
    ConfidenceMode confidence_mode;
    std::vector<ForecastEvent> events;
};

struct Forecast final {
    Money current_liquid;
    Money protected_floor;
    UtcTimestamp start;
    UtcTimestamp horizon_end;
    ConfidenceMode confidence_mode;
    std::vector<ForecastPoint> points;
};

enum class ForecastError {
    invalid_horizon,
    currency_mismatch,
    invalid_amount_model,
    arithmetic_overflow,
    invalid_event,
};

using ForecastResult = std::variant<Forecast, ForecastError>;

[[nodiscard]] ForecastResult calculate_forecast(const ForecastRequest& request) noexcept;

using ForecastRequestResult = std::variant<ForecastRequest, ForecastError>;

// Converts explicit planning records into forecast inputs. This is a
// projection seam: it does not mutate commitments, income, goals, or buffers.
[[nodiscard]] ForecastRequestResult build_forecast_request(
    const Money& initial_liquid,
    UtcTimestamp start,
    UtcTimestamp horizon_end,
    ConfidenceMode confidence_mode,
    const std::vector<Commitment>& commitments,
    const std::vector<IncomeStream>& income_streams,
    const std::vector<Goal>& goals,
    const std::optional<CashBuffer>& cash_buffer) noexcept;

struct SafeSpendResult final {
    Money amount;
    UtcTimestamp as_of;
    UtcTimestamp horizon_end;
    ConfidenceMode confidence_mode;
    Money current_liquid;
    Money protected_floor;
    Money commitment_total;
    Money protected_goal_total;
    Money expected_income_total;
    Money minimum_projected_balance;
    UtcTimestamp minimum_projected_at;
    std::vector<std::string> basis_event_ids;
    std::vector<std::string> warnings;
};

using SafeSpendResultOrError = std::variant<SafeSpendResult, ForecastError>;

[[nodiscard]] SafeSpendResultOrError calculate_safe_to_spend(
    const ForecastRequest& request) noexcept;

struct Scenario final {
    Money amount;
    UtcTimestamp date;
    std::string account_id;
    std::optional<std::string> category_id;
    std::optional<std::string> description;
};

struct ScenarioComparison final {
    Money before_safe_to_spend;
    Money after_safe_to_spend;
    Money minimum_projected_balance_before;
    Money minimum_projected_balance_after;
    std::vector<std::string> commitments_affected;
    std::vector<std::string> warnings;
};

using ScenarioResult = std::variant<ScenarioComparison, ForecastError>;

[[nodiscard]] ScenarioResult evaluate_scenario(
    const ForecastRequest& request,
    const Scenario& scenario) noexcept;

enum class InsightReason {
    low_buffer,
    negative_forecast,
    commitment_collision,
    unconfirmed_income_dependency,
    stale_balance,
};

enum class InsightSeverity {
    info,
    attention,
    warning,
    critical,
};

struct Insight final {
    std::string insight_id;
    InsightReason reason_code;
    InsightSeverity severity;
    std::string title;
    std::string explanation;
    UtcTimestamp effective_at;
    std::optional<UtcTimestamp> expires_at;
    Money amount_at_risk;
    std::vector<std::string> source_refs;
    std::vector<std::string> transaction_ids;
    std::vector<std::string> commitment_ids;
    std::vector<std::string> forecast_event_ids;
    std::vector<std::string> action_options;
};

[[nodiscard]] std::vector<Insight> build_insights(
    const Forecast& forecast,
    const SafeSpendResult& safe_spend,
    bool balance_is_stale) noexcept;

}  // namespace bundle
