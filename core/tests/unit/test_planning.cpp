#include "bundle/planning.hpp"

#include <cassert>
#include <chrono>
#include <limits>
#include <string>
#include <variant>

namespace {

bundle::CurrencyCode usd() {
    return *bundle::CurrencyCode::parse("USD");
}

bundle::AmountModel amount(const std::int64_t minor_units) {
    return bundle::AmountModel{
        .kind = bundle::AmountModelKind::fixed,
        .primary = bundle::Money{minor_units, usd()},
        .minimum = std::nullopt,
        .maximum = std::nullopt,
    };
}

std::int64_t day_millis(
    const int year_value,
    const unsigned month_value,
    const unsigned day_value) {
    using namespace std::chrono;
    const auto date = year_month_day{
        std::chrono::year{year_value},
        std::chrono::month{month_value},
        std::chrono::day{day_value}};
    return duration_cast<milliseconds>(sys_days{date}.time_since_epoch()).count();
}

bundle::ForecastRequest example_request(const bundle::ConfidenceMode mode) {
    const auto currency = usd();
    const auto start = bundle::UtcTimestamp::from_unix_millis(0);
    return bundle::ForecastRequest{
        .initial_liquid = bundle::Money{1'500, currency},
        .protected_floor = bundle::Money{200, currency},
        .start = start,
        .horizon_end = bundle::UtcTimestamp::from_unix_millis(2 * 86'400'000),
        .confidence_mode = mode,
        .events = {
            bundle::ForecastEvent{
                .event_id = "rent",
                .kind = bundle::ForecastEventKind::commitment,
                .expected_at = bundle::UtcTimestamp::from_unix_millis(86'400'000),
                .amount_model = amount(1'000),
                .direction = bundle::ForecastDirection::outflow,
                .confidence = bundle::Confidence::verified,
                .account_scope = "checking",
                .source_type = "user-confirmed",
                .source_ref = std::nullopt,
            },
            bundle::ForecastEvent{
                .event_id = "paycheck",
                .kind = bundle::ForecastEventKind::income,
                .expected_at = bundle::UtcTimestamp::from_unix_millis(2 * 86'400'000),
                .amount_model = amount(800),
                .direction = bundle::ForecastDirection::inflow,
                .confidence = bundle::Confidence::verified,
                .account_scope = "checking",
                .source_type = "user-confirmed",
                .source_ref = std::nullopt,
            },
            bundle::ForecastEvent{
                .event_id = "uncertain-income",
                .kind = bundle::ForecastEventKind::income,
                .expected_at = bundle::UtcTimestamp::from_unix_millis(86'400'000),
                .amount_model = amount(500),
                .direction = bundle::ForecastDirection::inflow,
                .confidence = bundle::Confidence::estimated,
                .account_scope = "checking",
                .source_type = "pattern",
                .source_ref = std::nullopt,
            },
        },
    };
}

void confirmed_and_expected_modes_do_not_collapse_confidence() {
    const auto confirmed = bundle::calculate_safe_to_spend(
        example_request(bundle::ConfidenceMode::confirmed));
    assert(std::holds_alternative<bundle::SafeSpendResult>(confirmed));
    assert(std::get<bundle::SafeSpendResult>(confirmed).amount.minor_units == 300);

    const auto expected = bundle::calculate_safe_to_spend(
        example_request(bundle::ConfidenceMode::expected));
    assert(std::holds_alternative<bundle::SafeSpendResult>(expected));
    assert(std::get<bundle::SafeSpendResult>(expected).amount.minor_units == 300);

    const auto stress = bundle::calculate_safe_to_spend(
        example_request(bundle::ConfidenceMode::stress));
    assert(std::holds_alternative<bundle::SafeSpendResult>(stress));
    assert(std::get<bundle::SafeSpendResult>(stress).expected_income_total.minor_units == 1'300);
}

void forecast_applies_timing_and_records_basis() {
    const auto request = example_request(bundle::ConfidenceMode::confirmed);
    const auto result = bundle::calculate_forecast(request);
    assert(std::holds_alternative<bundle::Forecast>(result));
    const auto& forecast = std::get<bundle::Forecast>(result);
    assert(forecast.points.size() == 3);
    assert(forecast.points[0].closing.minor_units == 1'500);
    assert(forecast.points[1].closing.minor_units == 500);
    assert(forecast.points[2].closing.minor_units == 1'300);
    assert(forecast.points[1].contributing_event_ids.size() == 1);
    assert(forecast.points[1].contributing_event_ids[0] == "rent");
}

void scenarios_are_overlays_and_never_mutate_request() {
    auto request = example_request(bundle::ConfidenceMode::confirmed);
    const auto original_event_count = request.events.size();
    const auto scenario = bundle::Scenario{
        .amount = bundle::Money{250, usd()},
        .date = bundle::UtcTimestamp::from_unix_millis(86'400'000),
        .account_id = "checking",
        .category_id = std::string{"dining"},
        .description = std::string{"hypothetical purchase"},
    };
    const auto result = bundle::evaluate_scenario(request, scenario);
    assert(std::holds_alternative<bundle::ScenarioComparison>(result));
    const auto& comparison = std::get<bundle::ScenarioComparison>(result);
    assert(comparison.before_safe_to_spend.minor_units == 300);
    assert(comparison.after_safe_to_spend.minor_units == 50);
    assert(request.events.size() == original_event_count);
}

void explicit_planning_records_become_typed_events() {
    const auto currency = usd();
    const auto start = bundle::UtcTimestamp::from_unix_millis(0);
    const auto due = bundle::UtcTimestamp::from_unix_millis(2 * 86'400'000LL);
    const auto request = bundle::build_forecast_request(
        bundle::Money{10'000, currency},
        start,
        bundle::UtcTimestamp::from_unix_millis(5 * 86'400'000LL),
        bundle::ConfidenceMode::expected,
        {
            bundle::Commitment{
                .commitment_id = "rent",
                .name = "Rent",
                .merchant_id = std::nullopt,
                .amount_model = amount(2'000),
                .currency = currency,
                .cadence = bundle::Cadence::monthly,
                .next_expected_at = due,
                .grace_window_millis = 0,
                .commitment_type = "housing",
                .status = bundle::CommitmentStatus::active,
                .confidence = bundle::Confidence::verified,
                .source_refs = {},
                .related_transaction_ids = {},
                .user_confirmed = true,
                .created_at = start,
                .updated_at = start,
            },
        },
        {
            bundle::IncomeStream{
                .income_id = "paycheck",
                .name = "Paycheck",
                .account_id = "checking",
                .payer_merchant_id = std::nullopt,
                .amount_model = amount(5'000),
                .cadence = bundle::Cadence::biweekly,
                .next_expected_at = due,
                .confidence = bundle::Confidence::supported,
                .source_refs = {},
                .related_transaction_ids = {},
                .include_in_forecast = true,
            },
        },
        {
            bundle::Goal{
                .goal_id = "emergency",
                .name = "Emergency fund",
                .target_amount = bundle::Money{20'000, currency},
                .target_date = bundle::UtcTimestamp::from_unix_millis(3 * 86'400'000LL),
                .current_reserved = bundle::Money{0, currency},
                .planned_contribution = bundle::Money{500, currency},
                .priority = 1,
                .protected_goal = true,
                .funding_account_ids = {"savings"},
                .status = bundle::GoalStatus::active,
            },
        },
        bundle::CashBuffer{
            .amount = bundle::Money{1'000, currency},
            .scope = "checking",
            .reason = "minimum floor",
        });
    assert(std::holds_alternative<bundle::ForecastRequest>(request));
    const auto& built = std::get<bundle::ForecastRequest>(request);
    assert(built.protected_floor.minor_units == 1'000);
    assert(built.events.size() == 3);
    assert(built.events[0].kind == bundle::ForecastEventKind::commitment);
    assert(built.events[1].kind == bundle::ForecastEventKind::income);
    assert(built.events[2].kind == bundle::ForecastEventKind::goal_contribution);
}

void recurring_series_expand_with_month_end_clamping() {
    const auto currency = usd();
    const auto start = bundle::UtcTimestamp::from_unix_millis(day_millis(2026, 1, 1));
    const auto horizon = bundle::UtcTimestamp::from_unix_millis(day_millis(2026, 4, 30));
    const auto result = bundle::build_forecast_request(
        bundle::Money{10'000, currency},
        start,
        horizon,
        bundle::ConfidenceMode::expected,
        {
            bundle::Commitment{
                .commitment_id = "monthly",
                .name = "Month end",
                .merchant_id = std::nullopt,
                .amount_model = amount(100),
                .currency = currency,
                .cadence = bundle::Cadence::monthly,
                .next_expected_at = bundle::UtcTimestamp::from_unix_millis(
                    day_millis(2026, 1, 31)),
                .grace_window_millis = 0,
                .commitment_type = "test",
                .status = bundle::CommitmentStatus::active,
                .confidence = bundle::Confidence::verified,
                .source_refs = {},
                .related_transaction_ids = {},
                .user_confirmed = true,
                .created_at = start,
                .updated_at = start,
            },
        },
        {},
        {},
        std::nullopt);
    assert(std::holds_alternative<bundle::ForecastRequest>(result));
    const auto& request = std::get<bundle::ForecastRequest>(result);
    assert(request.events.size() == 4);
    assert(request.events[0].expected_at.unix_millis == day_millis(2026, 1, 31));
    assert(request.events[1].expected_at.unix_millis == day_millis(2026, 2, 28));
    assert(request.events[2].expected_at.unix_millis == day_millis(2026, 3, 31));
    assert(request.events[3].expected_at.unix_millis == day_millis(2026, 4, 30));
}

void duplicate_event_ids_and_unbounded_ranges_fail_closed() {
    auto request = example_request(bundle::ConfidenceMode::confirmed);
    request.events[0].event_id = request.events[1].event_id;
    const auto duplicate = bundle::calculate_forecast(request);
    assert(std::holds_alternative<bundle::ForecastError>(duplicate));
    assert(std::get<bundle::ForecastError>(duplicate) == bundle::ForecastError::invalid_event);

    request = example_request(bundle::ConfidenceMode::confirmed);
    request.events[0].amount_model.kind = bundle::AmountModelKind::range;
    const auto unbounded = bundle::calculate_forecast(request);
    assert(std::holds_alternative<bundle::ForecastError>(unbounded));
    assert(std::get<bundle::ForecastError>(unbounded) ==
           bundle::ForecastError::invalid_amount_model);
}

void stale_and_negative_results_are_explicit_insights() {
    const auto request = example_request(bundle::ConfidenceMode::confirmed);
    const auto forecast_result = bundle::calculate_forecast(request);
    const auto safe_result = bundle::calculate_safe_to_spend(request);
    assert(std::holds_alternative<bundle::Forecast>(forecast_result));
    assert(std::holds_alternative<bundle::SafeSpendResult>(safe_result));
    const auto insights = bundle::build_insights(
        std::get<bundle::Forecast>(forecast_result),
        std::get<bundle::SafeSpendResult>(safe_result),
        true);
    assert(insights.size() == 1);
    assert(insights[0].reason_code == bundle::InsightReason::stale_balance);
}

void invalid_horizons_and_currency_mismatch_fail_closed() {
    auto request = example_request(bundle::ConfidenceMode::confirmed);
    request.horizon_end = bundle::UtcTimestamp::from_unix_millis(-1);
    const auto invalid = bundle::calculate_forecast(request);
    assert(std::get<bundle::ForecastError>(invalid) == bundle::ForecastError::invalid_horizon);

    request = example_request(bundle::ConfidenceMode::confirmed);
    request.protected_floor = bundle::Money{
        200,
        *bundle::CurrencyCode::parse("JPY"),
    };
    const auto mismatch = bundle::calculate_forecast(request);
    assert(std::get<bundle::ForecastError>(mismatch) == bundle::ForecastError::currency_mismatch);
}

void forecast_timestamp_limits_do_not_wrap() {
    const auto currency = usd();
    const auto maximum = std::numeric_limits<std::int64_t>::max();
    const auto request = bundle::ForecastRequest{
        .initial_liquid = bundle::Money{1'000, currency},
        .protected_floor = bundle::Money{0, currency},
        .start = bundle::UtcTimestamp::from_unix_millis(maximum - 100),
        .horizon_end = bundle::UtcTimestamp::from_unix_millis(maximum - 1),
        .confidence_mode = bundle::ConfidenceMode::confirmed,
        .events = {
            bundle::ForecastEvent{
                .event_id = "near-limit-charge",
                .kind = bundle::ForecastEventKind::known_one_time_charge,
                .expected_at = bundle::UtcTimestamp::from_unix_millis(maximum - 1),
                .amount_model = amount(100),
                .direction = bundle::ForecastDirection::outflow,
                .confidence = bundle::Confidence::verified,
                .account_scope = "checking",
                .source_type = "test",
                .source_ref = std::nullopt,
            },
        },
    };
    const auto result = bundle::calculate_forecast(request);
    assert(std::holds_alternative<bundle::Forecast>(result));
    const auto& forecast = std::get<bundle::Forecast>(result);
    assert(forecast.points.size() == 1);
    assert(forecast.points[0].closing.minor_units == 900);
    assert(forecast.points[0].contributing_event_ids.size() == 1);
}

}  // namespace

int main() {
    confirmed_and_expected_modes_do_not_collapse_confidence();
    forecast_applies_timing_and_records_basis();
    scenarios_are_overlays_and_never_mutate_request();
    explicit_planning_records_become_typed_events();
    recurring_series_expand_with_month_end_clamping();
    duplicate_event_ids_and_unbounded_ranges_fail_closed();
    stale_and_negative_results_are_explicit_insights();
    invalid_horizons_and_currency_mismatch_fail_closed();
    forecast_timestamp_limits_do_not_wrap();
    return 0;
}
