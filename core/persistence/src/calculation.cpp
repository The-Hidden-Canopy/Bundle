// Deterministic safe-to-spend over persisted state, recorded as a calculation
// receipt. The arithmetic is the existing C++ planning engine
// (bundle::calculate_safe_to_spend); this file only maps persisted rows to its
// inputs and persists the result under the receipt/event contract the Python
// bridge restores and re-exports.

#include "bundle/persistence/sqlite_store.hpp"

#include "bundle/persistence/text.hpp"
#include "bundle/planning.hpp"
#include "sqlite_db.hpp"

#include <algorithm>
#include <map>

namespace bundle::persistence {
using detail::Connection;
using detail::Row;
using detail::SqliteError;
using detail::WriteTransaction;

namespace {

[[noreturn]] void fail(const std::string& message) { throw StoreError(message); }

const char* k_engine_version_text() { return BUNDLE_PERSISTENCE_VERSION; }

template <typename E>
E lookup(const std::map<std::string, E>& table, const Json& value, const std::string& label) {
    if (value.is_string()) {
        if (const auto it = table.find(value.as_string()); it != table.end()) return it->second;
    }
    fail(label + " has a value the native calculation does not recognize: " + dump_json(value));
}

const std::map<std::string, CommitmentStatus> k_commitment_status = {
    {"candidate", CommitmentStatus::candidate}, {"active", CommitmentStatus::active},
    {"paused", CommitmentStatus::paused},       {"cancelled", CommitmentStatus::cancelled},
    {"ended", CommitmentStatus::ended},         {"unknown", CommitmentStatus::unknown}};
const std::map<std::string, Confidence> k_confidence = {
    {"verified", Confidence::verified}, {"supported", Confidence::supported}, {"inferred", Confidence::inferred},
    {"estimated", Confidence::estimated}, {"unknown", Confidence::unknown},   {"stale", Confidence::stale}};
const std::map<std::string, Cadence> k_cadence = {
    {"weekly", Cadence::weekly},         {"biweekly", Cadence::biweekly}, {"monthly", Cadence::monthly},
    {"quarterly", Cadence::quarterly},   {"semiannual", Cadence::semiannual}, {"annual", Cadence::annual},
    {"custom", Cadence::custom}};
const std::map<std::string, AmountModelKind> k_amount_kind = {
    {"fixed", AmountModelKind::fixed},           {"range", AmountModelKind::range},
    {"rolling_average", AmountModelKind::rolling_average}, {"last_amount", AmountModelKind::last_amount},
    {"seasonal", AmountModelKind::seasonal},     {"user_entered", AmountModelKind::user_entered}};
const std::map<std::string, GoalStatus> k_goal_status = {
    {"active", GoalStatus::active}, {"paused", GoalStatus::paused},
    {"completed", GoalStatus::completed}, {"cancelled", GoalStatus::cancelled}};
const std::map<std::string, ConfidenceMode> k_mode = {
    {"confirmed", ConfidenceMode::confirmed}, {"expected", ConfidenceMode::expected}, {"stress", ConfidenceMode::stress}};

const char* forecast_error_name(const ForecastError error) {
    switch (error) {
        case ForecastError::invalid_horizon: return "invalid_horizon";
        case ForecastError::currency_mismatch: return "currency_mismatch";
        case ForecastError::invalid_amount_model: return "invalid_amount_model";
        case ForecastError::arithmetic_overflow: return "arithmetic_overflow";
        case ForecastError::invalid_event: return "invalid_event";
    }
    return "unknown";
}

UtcTimestamp to_utc(const std::string& text, const std::string& label) {
    const auto parsed = parse_timestamp(text);
    if (!parsed.value) fail(label + " must be an RFC3339 timestamp with an explicit offset");
    const auto micros = parsed.value->unix_micros;
    return UtcTimestamp::from_unix_millis(micros >= 0 ? micros / 1000 : -((-micros + 999) / 1000));
}

std::string millis_text(const UtcTimestamp value) { return format_utc_micros(value.unix_millis * 1000); }

CurrencyCode currency_of(const Json& value, const std::string& label) {
    if (!value.is_string()) fail(label + " currency must be text");
    const auto code = CurrencyCode::parse(value.as_string());
    if (!code) fail(label + " currency " + value.as_string() + " is not a three-letter uppercase code");
    return *code;
}

Money money_of(const Json& value, const std::string& label) {
    if (!value.is_object() || !value.get("minor_units").is_int()) fail(label + " must be {currency, minor_units}");
    return Money{value.get("minor_units").as_int(), currency_of(value.get("currency"), label)};
}

Json money_json(const Money& money) { return Json::integer(money.minor_units); }

Json row_object(const Row& row, const std::vector<std::string>& columns) {
    Json::Object members;
    for (std::size_t i = 0; i < columns.size(); ++i) members.emplace_back(columns[i], row[i]);
    return Json::object(std::move(members));
}

}  // namespace

SafeToSpendReceipt SqliteFinanceStore::calculate_safe_to_spend(const SafeToSpendRequest& request) {
    const auto as_of_parse = parse_timestamp(request.as_of);
    const auto horizon_parse = parse_timestamp(request.horizon_end);
    if (!as_of_parse.value) fail("--as-of must be an RFC3339 timestamp with an explicit offset");
    if (!horizon_parse.value) fail("--horizon-end must be an RFC3339 timestamp with an explicit offset");
    const auto mode = lookup(k_mode, Json::string(request.confidence_mode), "confidence mode");
    if (request.protected_floor_minor < 0) fail("protected floor must not be negative");

    Connection c;
    c.db = db_;
    c.hook = &hook_;
    try {
        WriteTransaction transaction(c);  // read and record under one snapshot

        if (c.one("SELECT COUNT(*) FROM income_streams")->at(0).as_int() != 0) {
            fail("income_streams rows exist but the native calculation does not read them yet; "
                 "refusing to compute a result that would silently ignore income");
        }

        const std::vector<std::string> account_columns = {"account_id", "currency", "available_minor",
                                                          "balance_observed_at", "balance_freshness"};
        Json::Array account_inputs;
        std::vector<std::string> refs;
        std::optional<CurrencyCode> currency;
        std::int64_t liquid = 0;
        for (const auto& row : c.query("SELECT account_id, currency, available_minor, balance_observed_at, "
                                       "balance_freshness FROM accounts WHERE include_in_safe_to_spend = 1 "
                                       "AND status = 'active' ORDER BY account_id")) {
            const auto label = "account " + *python_str(row[0]);
            const auto code = currency_of(row[1], label);
            if (currency && *currency != code) {
                fail("accounts included in safe-to-spend use more than one currency; the native calculation "
                     "does not convert currencies");
            }
            currency = code;
            if (!row[2].is_int()) fail(label + ".available_minor is not an integer");
            if (__builtin_add_overflow(liquid, row[2].as_int(), &liquid)) fail("liquid balance total overflows");
            account_inputs.push_back(row_object(row, account_columns));
            refs.push_back("account:" + *python_str(row[0]));
        }
        if (!currency) fail("no active accounts are included in safe-to-spend");

        const std::vector<std::string> commitment_columns = {
            "commitment_id", "name", "merchant_id", "amount_model_json", "currency", "cadence",
            "next_expected_at", "grace_window_millis", "commitment_type", "status", "confidence",
            "user_confirmed", "created_at", "updated_at"};
        std::vector<Commitment> commitments;
        Json::Array commitment_inputs;
        std::string select = "SELECT ";
        for (std::size_t i = 0; i < commitment_columns.size(); ++i) select += (i ? ", " : "") + commitment_columns[i];
        for (const auto& row : c.query(select + " FROM commitments ORDER BY commitment_id")) {
            const auto id = *python_str(row[0]);
            const auto label = "commitment " + id;
            Json model;
            try {
                model = parse_json(row[3].as_string());
            } catch (const std::exception&) {
                fail(label + ".amount_model_json is invalid JSON");
            }
            if (!model.is_object()) fail(label + ".amount_model_json must contain an object");
            AmountModel amount{lookup(k_amount_kind, model.get("kind"), label + ".amount_model.kind"),
                               money_of(model.get("primary"), label + ".amount_model.primary"), std::nullopt,
                               std::nullopt};
            if (!model.get("minimum").is_null()) amount.minimum = money_of(model.get("minimum"), label + ".amount_model.minimum");
            if (!model.get("maximum").is_null()) amount.maximum = money_of(model.get("maximum"), label + ".amount_model.maximum");
            commitments.push_back(Commitment{
                .commitment_id = id,
                .name = *python_str(row[1]),
                .merchant_id = row[2].is_null() ? std::nullopt : python_str(row[2]),
                .amount_model = amount,
                .currency = currency_of(row[4], label),
                .cadence = lookup(k_cadence, row[5], label + ".cadence"),
                .next_expected_at = to_utc(*python_str(row[6]), label + ".next_expected_at"),
                .grace_window_millis = row[7].as_int(),
                .commitment_type = *python_str(row[8]),
                .status = lookup(k_commitment_status, row[9], label + ".status"),
                .confidence = lookup(k_confidence, row[10], label + ".confidence"),
                .source_refs = {},
                .related_transaction_ids = {},
                .user_confirmed = row[11].as_int() == 1,
                .created_at = to_utc(*python_str(row[12]), label + ".created_at"),
                .updated_at = to_utc(*python_str(row[13]), label + ".updated_at"),
            });
            auto input = row_object(row, commitment_columns);
            input.set("amount_model", model);
            input.erase("amount_model_json");
            commitment_inputs.push_back(std::move(input));
            refs.push_back("commitment:" + id);
        }

        std::map<std::string, std::vector<std::string>> plans;
        for (const auto& row : c.query("SELECT goal_id, account_id FROM goal_plans ORDER BY goal_id, account_id")) {
            plans[*python_str(row[0])].push_back(*python_str(row[1]));
        }
        const std::vector<std::string> goal_columns = {"goal_id", "name", "target_minor", "currency", "target_date",
                                                       "current_reserved_minor", "planned_contribution_minor",
                                                       "priority", "protected", "status"};
        std::vector<Goal> goals;
        Json::Array goal_inputs;
        for (const auto& row : c.query("SELECT goal_id, name, target_minor, currency, target_date, "
                                       "current_reserved_minor, planned_contribution_minor, priority, protected, "
                                       "status FROM goals ORDER BY goal_id")) {
            const auto id = *python_str(row[0]);
            const auto label = "goal " + id;
            const auto code = currency_of(row[3], label);
            goals.push_back(Goal{
                .goal_id = id,
                .name = *python_str(row[1]),
                .target_amount = Money{row[2].as_int(), code},
                .target_date = row[4].is_null() ? std::nullopt
                                                : std::optional<UtcTimestamp>(to_utc(*python_str(row[4]), label + ".target_date")),
                .current_reserved = Money{row[5].as_int(), code},
                .planned_contribution = Money{row[6].as_int(), code},
                .priority = static_cast<std::int32_t>(row[7].as_int()),
                .protected_goal = row[8].as_int() == 1,
                .funding_account_ids = plans[id],
                .status = lookup(k_goal_status, row[9], label + ".status"),
            });
            auto input = row_object(row, goal_columns);
            Json::Array funding;
            for (const auto& account : plans[id]) funding.push_back(Json::string(account));
            input.set("plans", Json::array(std::move(funding)));
            goal_inputs.push_back(std::move(input));
            refs.push_back("goal:" + id);
        }
        std::sort(refs.begin(), refs.end());

        const auto start = to_utc(as_of_parse.value->text, "as_of");
        const auto horizon = to_utc(horizon_parse.value->text, "horizon_end");
        const Money initial{liquid, *currency};
        const auto built = build_forecast_request(initial, start, horizon, mode, commitments, {}, goals,
                                                  CashBuffer{Money{request.protected_floor_minor, *currency},
                                                             "all-included-accounts", "user floor"});
        if (std::holds_alternative<ForecastError>(built)) {
            fail(std::string("forecast inputs rejected: ") + forecast_error_name(std::get<ForecastError>(built)));
        }
        const auto calculated = bundle::calculate_safe_to_spend(std::get<ForecastRequest>(built));
        if (std::holds_alternative<ForecastError>(calculated)) {
            fail(std::string("safe-to-spend calculation failed: ") +
                 forecast_error_name(std::get<ForecastError>(calculated)));
        }
        const auto& result = std::get<SafeSpendResult>(calculated);
        const auto& forecast_request = std::get<ForecastRequest>(built);

        // Projected points for display; not part of the receipt or the archive.
        Json::Array points;
        if (const auto forecast = calculate_forecast(forecast_request); std::holds_alternative<Forecast>(forecast)) {
            for (const auto& point : std::get<Forecast>(forecast).points) {
                Json::Array ids;
                for (const auto& id : point.contributing_event_ids) ids.push_back(Json::string(id));
                points.push_back(Json::object({
                    {"date", Json::string(millis_text(point.date))},
                    {"opening_minor", money_json(point.opening)},
                    {"inflows_minor", money_json(point.inflows)},
                    {"outflows_minor", money_json(point.outflows)},
                    {"closing_minor", money_json(point.closing)},
                    {"floor_minor", money_json(point.floor)},
                    {"event_ids", Json::array(std::move(ids))},
                }));
            }
        }
        Json what_if;
        if (request.what_if) {
            const auto& spend = *request.what_if;
            if (spend.amount_minor <= 0) fail("what-if amount must be a positive number of minor units");
            const auto evaluated = evaluate_scenario(
                forecast_request, Scenario{Money{spend.amount_minor, *currency}, to_utc(spend.date, "what-if date"),
                                           spend.account_id, std::nullopt, spend.description});
            if (std::holds_alternative<ForecastError>(evaluated)) {
                fail(std::string("what-if rejected: ") + forecast_error_name(std::get<ForecastError>(evaluated)));
            }
            const auto& comparison = std::get<ScenarioComparison>(evaluated);
            Json::Array affected;
            for (const auto& id : comparison.commitments_affected) affected.push_back(Json::string(id));
            Json::Array notes;
            for (const auto& note : comparison.warnings) notes.push_back(Json::string(note));
            what_if = Json::object({
                {"amount_minor", Json::integer(spend.amount_minor)},
                {"date", Json::string(parse_timestamp(spend.date).value->text)},
                {"safe_to_spend_before_minor", money_json(comparison.before_safe_to_spend)},
                {"safe_to_spend_after_minor", money_json(comparison.after_safe_to_spend)},
                {"minimum_balance_before_minor", money_json(comparison.minimum_projected_balance_before)},
                {"minimum_balance_after_minor", money_json(comparison.minimum_projected_balance_after)},
                {"commitments_affected", Json::array(std::move(affected))},
                {"warnings", Json::array(std::move(notes))},
            });
        }

        const Json inputs = Json::object({
            {"engine", Json::string("bundle.safe_to_spend")},
            {"engine_version", Json::string(k_engine_version_text())},
            {"as_of", Json::string(as_of_parse.value->text)},
            {"horizon_end", Json::string(horizon_parse.value->text)},
            {"confidence_mode", Json::string(request.confidence_mode)},
            {"protected_floor_minor", Json::integer(request.protected_floor_minor)},
            {"currency", Json::string(currency->value())},
            {"accounts", Json::array(account_inputs)},
            {"commitments", Json::array(commitment_inputs)},
            {"goals", Json::array(goal_inputs)},
        });
        const auto input_hash = sha256_tagged(canonical_json(inputs));
        Json::Array basis;
        for (const auto& id : result.basis_event_ids) basis.push_back(Json::string(id));
        Json::Array warnings;
        for (const auto& warning : result.warnings) warnings.push_back(Json::string(warning));
        const Json outputs = Json::object({
            {"currency", Json::string(currency->value())},
            {"safe_to_spend_minor", money_json(result.amount)},
            {"current_liquid_minor", money_json(result.current_liquid)},
            {"protected_floor_minor", money_json(result.protected_floor)},
            {"commitment_total_minor", money_json(result.commitment_total)},
            {"protected_goal_total_minor", money_json(result.protected_goal_total)},
            {"expected_income_total_minor", money_json(result.expected_income_total)},
            {"minimum_projected_balance_minor", money_json(result.minimum_projected_balance)},
            {"minimum_projected_at", Json::string(millis_text(result.minimum_projected_at))},
            {"basis_event_ids", Json::array(std::move(basis))},
            {"warnings", Json::array(std::move(warnings))},
        });
        Json::Array ref_values;
        for (const auto& ref : refs) ref_values.push_back(Json::string(ref));
        const auto receipt_id = "safe-to-spend:" + input_hash.substr(7, 24);
        const Json record = Json::object({
            {"receipt_id", Json::string(receipt_id)},
            {"engine", Json::string("bundle.safe_to_spend")},
            {"engine_version", Json::string(k_engine_version_text())},
            {"calculated_at", Json::string(as_of_parse.value->text)},
            {"input_refs", Json::array(std::move(ref_values))},
            {"input_hash", Json::string(input_hash)},
            {"policy_id", Json::string(request.confidence_mode)},
            {"policy_version", Json::string("1")},
            {"outputs", outputs},
        });
        const Row stored = {record.get("receipt_id"), record.get("engine"), record.get("engine_version"),
                            record.get("calculated_at"), Json::string(canonical_json(record.get("input_refs"))),
                            record.get("input_hash"), record.get("policy_id"), record.get("policy_version"),
                            Json::string(canonical_json(outputs))};
        if (!request.record || request.what_if) {
            // Compute-only: the write transaction is rolled back when it goes out of scope.
            const bool known = c.one("SELECT 1 FROM calculation_receipts WHERE receipt_id = ?",
                                     {record.get("receipt_id")}).has_value();
            return SafeToSpendReceipt{receipt_id, false, record, Json::array(std::move(points)), what_if,
                                      known};
        }
        const auto existing = c.one("SELECT receipt_id, engine, engine_version, calculated_at, input_refs_json, "
                                    "input_hash, policy_id, policy_version, outputs_json FROM calculation_receipts "
                                    "WHERE receipt_id = ?",
                                    {record.get("receipt_id")});
        bool inserted = false;
        if (existing) {
            if (*existing != stored) fail("calculation receipt " + receipt_id + " conflicts with existing local data");
        } else {
            c.run("INSERT INTO calculation_receipts (receipt_id, engine, engine_version, calculated_at, "
                  "input_refs_json, input_hash, policy_id, policy_version, outputs_json) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)",
                  stored);
            inserted = true;
        }
        // Same event identity and hash the archive restore path regenerates.
        const auto event_id = "calculation-receipt:" + receipt_id;
        const auto event_hash = sha256_tagged(canonical_json(record));
        const auto event = c.one("SELECT event_type, object_id, occurred_at, content_hash FROM event_log WHERE event_id = ?",
                                 {Json::string(event_id)});
        const Row expected_event = {Json::string("calculation_receipt.appended"), Json::string(receipt_id),
                                    Json::string(as_of_parse.value->text), Json::string(event_hash)};
        if (event) {
            if (*event != expected_event) fail("event conflict for '" + event_id + "'");
        } else {
            c.run("INSERT INTO event_log (event_id, event_type, object_id, occurred_at, content_hash) VALUES (?, ?, ?, ?, ?)",
                  {Json::string(event_id), expected_event[0], expected_event[1], expected_event[2], expected_event[3]});
        }
        transaction.commit();
        return SafeToSpendReceipt{receipt_id, inserted, record, Json::array(std::move(points)), what_if, true};
    } catch (const SqliteError& error) {
        fail(std::string("SQLite calculation receipt failed: ") + error.what());
    }
}

}  // namespace bundle::persistence
