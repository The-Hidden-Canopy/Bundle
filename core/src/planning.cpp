#include "bundle/planning.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>
#include <set>
#include <utility>

namespace bundle {
namespace {

constexpr std::int64_t millis_per_day = 86'400'000;
constexpr std::size_t maximum_occurrences_per_series = 10'000;

bool confidence_is_included(const Confidence confidence, const ConfidenceMode mode) {
    if (confidence == Confidence::stale || confidence == Confidence::unknown) {
        return false;
    }
    if (mode == ConfidenceMode::confirmed) {
        return confidence == Confidence::verified;
    }
    if (mode == ConfidenceMode::expected) {
        return confidence == Confidence::verified || confidence == Confidence::supported ||
               confidence == Confidence::inferred;
    }
    return confidence != Confidence::unknown && confidence != Confidence::stale;
}

bool same_currency(const Money& left, const Money& right) {
    return left.currency == right.currency;
}

std::optional<Money> resolved_amount(
    const AmountModel& amount_model,
    const ConfidenceMode mode) {
    if (amount_model.kind == AmountModelKind::range && mode == ConfidenceMode::stress &&
        amount_model.maximum.has_value()) {
        return amount_model.maximum;
    }
    if (amount_model.kind == AmountModelKind::range &&
        amount_model.minimum.has_value() && amount_model.maximum.has_value() &&
        amount_model.minimum->currency == amount_model.maximum->currency &&
        amount_model.primary.currency == amount_model.minimum->currency) {
        const auto minimum = amount_model.minimum->minor_units;
        const auto maximum = amount_model.maximum->minor_units;
        if (minimum > maximum) {
            return std::nullopt;
        }
    }
    return amount_model.primary;
}

std::optional<Money> safe_add(const Money& left, const Money& right) {
    const auto result = add(left, right);
    if (!std::holds_alternative<Money>(result)) {
        return std::nullopt;
    }
    return std::get<Money>(result);
}

std::optional<Money> safe_subtract(const Money& left, const Money& right) {
    const auto result = subtract(left, right);
    if (!std::holds_alternative<Money>(result)) {
        return std::nullopt;
    }
    return std::get<Money>(result);
}

std::optional<ForecastError> validate_event(
    const ForecastEvent& event,
    const CurrencyCode& currency,
    const ConfidenceMode mode) {
    if (event.event_id.empty()) {
        return ForecastError::invalid_event;
    }
    if (!confidence_is_included(event.confidence, mode)) {
        return std::nullopt;
    }
    if (event.amount_model.primary.currency != currency) {
        return ForecastError::currency_mismatch;
    }
    const auto amount = resolved_amount(event.amount_model, mode);
    if (!amount.has_value() || amount->currency != currency || amount->minor_units < 0) {
        return amount.has_value() && amount->currency != currency
                   ? ForecastError::currency_mismatch
                   : ForecastError::invalid_amount_model;
    }
    if (event.amount_model.minimum.has_value() &&
        event.amount_model.minimum->currency != currency) {
        return ForecastError::currency_mismatch;
    }
    if (event.amount_model.maximum.has_value() &&
        event.amount_model.maximum->currency != currency) {
        return ForecastError::currency_mismatch;
    }
    if (event.amount_model.minimum.has_value() &&
        event.amount_model.minimum->minor_units < 0) {
        return ForecastError::invalid_amount_model;
    }
    if (event.amount_model.maximum.has_value() &&
        event.amount_model.maximum->minor_units < 0) {
        return ForecastError::invalid_amount_model;
    }
    if (event.amount_model.kind == AmountModelKind::range &&
        (!event.amount_model.minimum.has_value() ||
         !event.amount_model.maximum.has_value())) {
        return ForecastError::invalid_amount_model;
    }
    if (event.amount_model.minimum.has_value() && event.amount_model.maximum.has_value() &&
        event.amount_model.minimum->minor_units > event.amount_model.maximum->minor_units) {
        return ForecastError::invalid_amount_model;
    }
    return std::nullopt;
}

std::optional<ForecastError> add_to(Money& target, const Money& amount) {
    const auto result = safe_add(target, amount);
    if (!result.has_value()) {
        return ForecastError::arithmetic_overflow;
    }
    target = *result;
    return std::nullopt;
}

bool valid_amount_model(const AmountModel& amount_model, const CurrencyCode& currency) {
    if (amount_model.primary.currency != currency || amount_model.primary.minor_units < 0) {
        return false;
    }
    if (amount_model.minimum.has_value() &&
        (amount_model.minimum->currency != currency || amount_model.minimum->minor_units < 0)) {
        return false;
    }
    if (amount_model.maximum.has_value() &&
        (amount_model.maximum->currency != currency || amount_model.maximum->minor_units < 0)) {
        return false;
    }
    if (amount_model.kind == AmountModelKind::range &&
        (!amount_model.minimum.has_value() || !amount_model.maximum.has_value())) {
        return false;
    }
    return !amount_model.minimum.has_value() || !amount_model.maximum.has_value() ||
           amount_model.minimum->minor_units <= amount_model.maximum->minor_units;
}

bool checked_add_millis(
    const UtcTimestamp base,
    const std::int64_t delta,
    UtcTimestamp& result) {
    if ((delta > 0 && base.unix_millis > std::numeric_limits<std::int64_t>::max() - delta) ||
        (delta < 0 && base.unix_millis < std::numeric_limits<std::int64_t>::min() - delta)) {
        return false;
    }
    result = UtcTimestamp::from_unix_millis(base.unix_millis + delta);
    return true;
}

std::optional<UtcTimestamp> add_calendar_months(
    const UtcTimestamp base,
    const int month_delta,
    const unsigned anchor_day) {
    using namespace std::chrono;
    using Milliseconds = duration<std::int64_t, std::milli>;
    const sys_time<Milliseconds> instant{Milliseconds{base.unix_millis}};
    const auto day_floor = floor<days>(instant);
    const auto date = year_month_day{day_floor};
    if (!date.ok()) {
        return std::nullopt;
    }
    const auto shifted = year_month{date.year(), date.month()} + months{month_delta};
    if (!shifted.ok()) {
        return std::nullopt;
    }
    const auto last_day = static_cast<unsigned>(
        (year_month_day_last{shifted.year(), month_day_last{shifted.month()}}).day());
    const auto target_day = day{std::min(anchor_day, last_day)};
    const year_month_day target = shifted / target_day;
    const auto time_of_day = instant - day_floor;
    const sys_time<Milliseconds> result =
        sys_time<Milliseconds>{sys_days{target}.time_since_epoch()} + time_of_day;
    return UtcTimestamp::from_unix_millis(result.time_since_epoch().count());
}

std::optional<UtcTimestamp> advance_cadence(
    const UtcTimestamp base,
    const Cadence cadence,
    const unsigned anchor_day) {
    UtcTimestamp result{};
    switch (cadence) {
        case Cadence::weekly:
            if (!checked_add_millis(base, 7 * millis_per_day, result)) {
                return std::nullopt;
            }
            return result;
        case Cadence::biweekly:
            if (!checked_add_millis(base, 14 * millis_per_day, result)) {
                return std::nullopt;
            }
            return result;
        case Cadence::monthly:
            return add_calendar_months(base, 1, anchor_day);
        case Cadence::quarterly:
            return add_calendar_months(base, 3, anchor_day);
        case Cadence::semiannual:
            return add_calendar_months(base, 6, anchor_day);
        case Cadence::annual:
            return add_calendar_months(base, 12, anchor_day);
        case Cadence::custom:
            return std::nullopt;
    }
    return std::nullopt;
}

std::optional<SourceRef> first_source(const std::vector<SourceRef>& source_refs) {
    if (source_refs.empty()) {
        return std::nullopt;
    }
    return source_refs.front();
}

}  // namespace

ForecastResult calculate_forecast(const ForecastRequest& request) noexcept {
    if (request.horizon_end < request.start) {
        return ForecastError::invalid_horizon;
    }
    if (!same_currency(request.initial_liquid, request.protected_floor)) {
        return ForecastError::currency_mismatch;
    }

    std::vector<ForecastEvent> events;
    events.reserve(request.events.size());
    std::set<std::string> event_ids;
    for (const auto& event : request.events) {
        if (event.expected_at < request.start || event.expected_at > request.horizon_end) {
            continue;
        }
        if (!event_ids.insert(event.event_id).second) {
            return ForecastError::invalid_event;
        }
        if (const auto error = validate_event(
                event, request.initial_liquid.currency, request.confidence_mode);
            error.has_value()) {
            return *error;
        }
        if (confidence_is_included(event.confidence, request.confidence_mode)) {
            events.push_back(event);
        }
    }
    std::sort(events.begin(), events.end(), [](const ForecastEvent& left, const ForecastEvent& right) {
        if (left.expected_at == right.expected_at) {
            return left.event_id < right.event_id;
        }
        return left.expected_at < right.expected_at;
    });

    const auto horizon_millis = static_cast<std::uint64_t>(request.horizon_end.unix_millis) -
                                static_cast<std::uint64_t>(request.start.unix_millis);
    const auto maximum_horizon_millis = static_cast<std::uint64_t>(36'500) *
                                        static_cast<std::uint64_t>(millis_per_day);
    if (horizon_millis > maximum_horizon_millis) {
        return ForecastError::invalid_horizon;
    }
    const auto day_count = static_cast<std::int64_t>(horizon_millis /
                                                     static_cast<std::uint64_t>(millis_per_day));

    Forecast forecast{
        .current_liquid = request.initial_liquid,
        .protected_floor = request.protected_floor,
        .start = request.start,
        .horizon_end = request.horizon_end,
        .confidence_mode = request.confidence_mode,
        .points = {},
    };
    forecast.points.reserve(static_cast<std::size_t>(day_count) + 1);

    Money current = request.initial_liquid;
    std::size_t event_index = 0;
    for (std::int64_t day = 0; day <= day_count; ++day) {
        UtcTimestamp date{};
        if (!checked_add_millis(request.start, day * millis_per_day, date)) {
            return ForecastError::arithmetic_overflow;
        }
        UtcTimestamp next_date{};
        const auto has_next_date = checked_add_millis(date, millis_per_day, next_date);
        const auto final_day = day == day_count;
        if (!has_next_date && !final_day) {
            return ForecastError::arithmetic_overflow;
        }
        Money inflows{0, current.currency};
        Money outflows{0, current.currency};
        std::vector<std::string> event_ids;
        while (event_index < events.size() &&
               ((!has_next_date && final_day &&
                 events[event_index].expected_at <= request.horizon_end) ||
                (has_next_date && events[event_index].expected_at < next_date))) {
            const auto& event = events[event_index];
            const auto amount = *resolved_amount(event.amount_model, request.confidence_mode);
            if (event.direction == ForecastDirection::inflow) {
                if (const auto error = add_to(inflows, amount); error.has_value()) {
                    return *error;
                }
            } else {
                if (const auto error = add_to(outflows, amount); error.has_value()) {
                    return *error;
                }
            }
            event_ids.push_back(event.event_id);
            ++event_index;
        }
        const auto after_inflow = safe_add(current, inflows);
        if (!after_inflow.has_value()) {
            return ForecastError::arithmetic_overflow;
        }
        const auto closing = safe_subtract(*after_inflow, outflows);
        if (!closing.has_value()) {
            return ForecastError::arithmetic_overflow;
        }
        forecast.points.push_back(ForecastPoint{
            .date = date,
            .opening = current,
            .inflows = inflows,
            .outflows = outflows,
            .closing = *closing,
            .floor = request.protected_floor,
            .confidence = request.confidence_mode == ConfidenceMode::confirmed
                              ? Confidence::verified
                              : Confidence::supported,
            .contributing_event_ids = std::move(event_ids),
        });
        current = *closing;
    }
    return forecast;
}

ForecastRequestResult build_forecast_request(
    const Money& initial_liquid,
    const UtcTimestamp start,
    const UtcTimestamp horizon_end,
    const ConfidenceMode confidence_mode,
    const std::vector<Commitment>& commitments,
    const std::vector<IncomeStream>& income_streams,
    const std::vector<Goal>& goals,
    const std::optional<CashBuffer>& cash_buffer) noexcept {
    if (horizon_end < start) {
        return ForecastError::invalid_horizon;
    }
    Money protected_floor{0, initial_liquid.currency};
    if (cash_buffer.has_value()) {
        if (cash_buffer->amount.currency != initial_liquid.currency ||
            cash_buffer->amount.minor_units < 0) {
            return ForecastError::currency_mismatch;
        }
        protected_floor = cash_buffer->amount;
    }

    ForecastRequest request{
        .initial_liquid = initial_liquid,
        .protected_floor = protected_floor,
        .start = start,
        .horizon_end = horizon_end,
        .confidence_mode = confidence_mode,
        .events = {},
    };

    std::set<std::string> event_ids;
    const auto append_series = [&](const std::string& prefix,
                                   const std::string& object_id,
                                   const Cadence cadence,
                                   const UtcTimestamp first_at,
                                   const AmountModel& amount_model,
                                   const ForecastEventKind kind,
                                   const ForecastDirection direction,
                                   const Confidence confidence,
                                   const std::string& account_scope,
                                   const std::string& source_type,
                                   const std::optional<SourceRef>& source_ref)
        -> std::optional<ForecastError> {
        if (object_id.empty() || first_at < start || first_at > horizon_end) {
            return ForecastError::invalid_event;
        }
        if (!valid_amount_model(amount_model, initial_liquid.currency)) {
            return ForecastError::invalid_amount_model;
        }
        using namespace std::chrono;
        const auto first_date = year_month_day{
            floor<days>(sys_time<milliseconds>{milliseconds{first_at.unix_millis}})};
        if (!first_date.ok()) {
            return ForecastError::invalid_event;
        }
        const auto anchor_day = static_cast<unsigned>(first_date.day());
        UtcTimestamp occurrence = first_at;
        std::size_t occurrence_index = 1;
        for (;;) {
            if (occurrence > horizon_end) {
                break;
            }
            if (occurrence_index > maximum_occurrences_per_series) {
                return ForecastError::invalid_event;
            }
            const auto event_id = prefix + object_id + ":" +
                                  std::to_string(occurrence_index);
            if (!event_ids.insert(event_id).second) {
                return ForecastError::invalid_event;
            }
            request.events.push_back(ForecastEvent{
                .event_id = event_id,
                .kind = kind,
                .expected_at = occurrence,
                .amount_model = amount_model,
                .direction = direction,
                .confidence = confidence,
                .account_scope = account_scope,
                .source_type = source_type,
                .source_ref = source_ref,
            });
            if (cadence == Cadence::custom) {
                break;
            }
            const auto next = advance_cadence(occurrence, cadence, anchor_day);
            if (!next.has_value() || *next <= occurrence) {
                return ForecastError::invalid_event;
            }
            occurrence = *next;
            ++occurrence_index;
        }
        return std::nullopt;
    };

    for (const auto& commitment : commitments) {
        if (commitment.status == CommitmentStatus::paused ||
            commitment.status == CommitmentStatus::cancelled ||
            commitment.status == CommitmentStatus::ended ||
            commitment.next_expected_at < start || commitment.next_expected_at > horizon_end) {
            continue;
        }
        if (const auto error = append_series(
                "commitment:",
                commitment.commitment_id,
                commitment.cadence,
                commitment.next_expected_at,
                commitment.amount_model,
                ForecastEventKind::commitment,
                ForecastDirection::outflow,
                commitment.confidence,
                commitment.merchant_id.value_or(""),
                "commitment",
                first_source(commitment.source_refs));
            error.has_value()) {
            return *error;
        }
    }

    for (const auto& income : income_streams) {
        if (!income.include_in_forecast || income.next_expected_at < start ||
            income.next_expected_at > horizon_end) {
            continue;
        }
        if (const auto error = append_series(
                "income:",
                income.income_id,
                income.cadence,
                income.next_expected_at,
                income.amount_model,
                ForecastEventKind::income,
                ForecastDirection::inflow,
                income.confidence,
                income.account_id,
                "income_stream",
                first_source(income.source_refs));
            error.has_value()) {
            return *error;
        }
    }

    for (const auto& goal : goals) {
        if (goal.status != GoalStatus::active || !goal.protected_goal ||
            !goal.target_date.has_value() || goal.planned_contribution.minor_units <= 0 ||
            *goal.target_date < start || *goal.target_date > horizon_end) {
            continue;
        }
        if (goal.planned_contribution.currency != initial_liquid.currency) {
            return ForecastError::currency_mismatch;
        }
        if (goal.goal_id.empty()) {
            return ForecastError::invalid_event;
        }
        const auto goal_event_id = "goal:" + goal.goal_id;
        if (!event_ids.insert(goal_event_id).second) {
            return ForecastError::invalid_event;
        }
        request.events.push_back(ForecastEvent{
            .event_id = goal_event_id,
            .kind = ForecastEventKind::goal_contribution,
            .expected_at = *goal.target_date,
            .amount_model = AmountModel{
                .kind = AmountModelKind::user_entered,
                .primary = goal.planned_contribution,
                .minimum = std::nullopt,
                .maximum = std::nullopt,
            },
            .direction = ForecastDirection::outflow,
            .confidence = Confidence::supported,
            .account_scope = goal.funding_account_ids.empty()
                                 ? ""
                                 : goal.funding_account_ids.front(),
            .source_type = "protected_goal",
            .source_ref = std::nullopt,
        });
    }

    return request;
}

SafeSpendResultOrError calculate_safe_to_spend(const ForecastRequest& request) noexcept {
    const auto forecast_result = calculate_forecast(request);
    if (!std::holds_alternative<Forecast>(forecast_result)) {
        return std::get<ForecastError>(forecast_result);
    }
    const auto& forecast = std::get<Forecast>(forecast_result);
    if (forecast.points.empty()) {
        return ForecastError::invalid_horizon;
    }

    auto minimum_point = std::min_element(
        forecast.points.begin(), forecast.points.end(),
        [](const ForecastPoint& left, const ForecastPoint& right) {
            return left.closing.minor_units < right.closing.minor_units;
        });
    const auto surplus = safe_subtract(minimum_point->closing, request.protected_floor);
    if (!surplus.has_value()) {
        return ForecastError::arithmetic_overflow;
    }

    Money commitment_total{0, request.initial_liquid.currency};
    Money protected_goal_total{0, request.initial_liquid.currency};
    Money expected_income_total{0, request.initial_liquid.currency};
    std::vector<std::string> basis_event_ids = minimum_point->contributing_event_ids;
    for (const auto& event : request.events) {
        if (event.expected_at < request.start || event.expected_at > request.horizon_end ||
            !confidence_is_included(event.confidence, request.confidence_mode)) {
            continue;
        }
        const auto amount = resolved_amount(event.amount_model, request.confidence_mode);
        if (!amount.has_value()) {
            return ForecastError::invalid_amount_model;
        }
        if (event.kind == ForecastEventKind::commitment ||
            event.kind == ForecastEventKind::planned_transfer) {
            if (const auto result = safe_add(commitment_total, *amount); result.has_value()) {
                commitment_total = *result;
            } else {
                return ForecastError::arithmetic_overflow;
            }
        }
        if (event.kind == ForecastEventKind::goal_contribution ||
            event.kind == ForecastEventKind::budget_reserve) {
            if (const auto result = safe_add(protected_goal_total, *amount); result.has_value()) {
                protected_goal_total = *result;
            } else {
                return ForecastError::arithmetic_overflow;
            }
        }
        if (event.kind == ForecastEventKind::income &&
            event.direction == ForecastDirection::inflow) {
            if (const auto result = safe_add(expected_income_total, *amount); result.has_value()) {
                expected_income_total = *result;
            } else {
                return ForecastError::arithmetic_overflow;
            }
        }
    }

    std::vector<std::string> warnings;
    if (surplus->minor_units < 0) {
        warnings.emplace_back("protected floor is already violated in the selected horizon");
    }
    return SafeSpendResult{
        .amount = *surplus,
        .as_of = request.start,
        .horizon_end = request.horizon_end,
        .confidence_mode = request.confidence_mode,
        .current_liquid = request.initial_liquid,
        .protected_floor = request.protected_floor,
        .commitment_total = commitment_total,
        .protected_goal_total = protected_goal_total,
        .expected_income_total = expected_income_total,
        .minimum_projected_balance = minimum_point->closing,
        .minimum_projected_at = minimum_point->date,
        .basis_event_ids = std::move(basis_event_ids),
        .warnings = std::move(warnings),
    };
}

ScenarioResult evaluate_scenario(
    const ForecastRequest& request,
    const Scenario& scenario) noexcept {
    if (scenario.amount.currency != request.initial_liquid.currency ||
        scenario.amount.minor_units < 0) {
        return ForecastError::currency_mismatch;
    }
    const auto before_result = calculate_safe_to_spend(request);
    if (!std::holds_alternative<SafeSpendResult>(before_result)) {
        return std::get<ForecastError>(before_result);
    }

    auto with_scenario = request;
    with_scenario.events.push_back(ForecastEvent{
        .event_id = "scenario",
        .kind = ForecastEventKind::known_one_time_charge,
        .expected_at = scenario.date,
        .amount_model = AmountModel{
            .kind = AmountModelKind::user_entered,
            .primary = scenario.amount,
            .minimum = std::nullopt,
            .maximum = std::nullopt,
        },
        .direction = ForecastDirection::outflow,
        .confidence = Confidence::verified,
        .account_scope = scenario.account_id,
        .source_type = "scenario",
        .source_ref = std::nullopt,
    });
    const auto after_result = calculate_safe_to_spend(with_scenario);
    if (!std::holds_alternative<SafeSpendResult>(after_result)) {
        return std::get<ForecastError>(after_result);
    }
    const auto& before = std::get<SafeSpendResult>(before_result);
    const auto& after = std::get<SafeSpendResult>(after_result);
    return ScenarioComparison{
        .before_safe_to_spend = before.amount,
        .after_safe_to_spend = after.amount,
        .minimum_projected_balance_before = before.minimum_projected_balance,
        .minimum_projected_balance_after = after.minimum_projected_balance,
        .commitments_affected = {},
        .warnings = after.warnings,
    };
}

std::vector<Insight> build_insights(
    const Forecast& forecast,
    const SafeSpendResult& safe_spend,
    const bool balance_is_stale) noexcept {
    std::vector<Insight> insights;
    if (balance_is_stale) {
        insights.push_back(Insight{
            .insight_id = "stale-balance",
            .reason_code = InsightReason::stale_balance,
            .severity = InsightSeverity::attention,
            .title = "Balance data is stale",
            .explanation = "I do not have a fresh enough balance to call this safely.",
            .effective_at = forecast.start,
            .expires_at = std::nullopt,
            .amount_at_risk = safe_spend.amount,
            .source_refs = {},
            .transaction_ids = {},
            .commitment_ids = {},
            .forecast_event_ids = {},
            .action_options = {"refresh account data", "review last observation"},
        });
    }
    if (safe_spend.amount.minor_units < 0) {
        insights.push_back(Insight{
            .insight_id = "negative-forecast",
            .reason_code = InsightReason::negative_forecast,
            .severity = InsightSeverity::critical,
            .title = "Projected balance falls below the protected floor",
            .explanation = "The selected forecast includes a projected shortfall before the horizon ends.",
            .effective_at = safe_spend.minimum_projected_at,
            .expires_at = std::nullopt,
            .amount_at_risk = safe_spend.amount,
            .source_refs = {},
            .transaction_ids = {},
            .commitment_ids = {},
            .forecast_event_ids = safe_spend.basis_event_ids,
            .action_options = {"review commitments", "review expected income", "adjust protected plans"},
        });
    } else if (safe_spend.amount.minor_units == 0) {
        insights.push_back(Insight{
            .insight_id = "low-buffer",
            .reason_code = InsightReason::low_buffer,
            .severity = InsightSeverity::warning,
            .title = "No discretionary cash remains in the selected horizon",
            .explanation = "The projected balance reaches the protected floor.",
            .effective_at = safe_spend.minimum_projected_at,
            .expires_at = std::nullopt,
            .amount_at_risk = safe_spend.amount,
            .source_refs = {},
            .transaction_ids = {},
            .commitment_ids = {},
            .forecast_event_ids = safe_spend.basis_event_ids,
            .action_options = {"review protected plans"},
        });
    }
    return insights;
}

}  // namespace bundle
