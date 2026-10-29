// Read-only snapshot for user interfaces. Nothing here writes to the database.
//
// Transaction classification comes from the core's reconciliation engine
// (bundle::recognize_transfers / recognize_refunds); this file only maps rows to
// its inputs and labels the result. Matching needs posted or authorized times,
// which canonical CSV and OFX imports do not carry, so their rows stay
// "spending"/"income" and are marked as not matchable.

#include "bundle/persistence/sqlite_store.hpp"

#include "bundle/persistence/text.hpp"
#include "bundle/reconciliation.hpp"
#include "sqlite_db.hpp"

#include <map>
#include <string>
#include <vector>

namespace bundle::persistence {
using detail::Connection;
using detail::Row;
using detail::SqliteError;

namespace {

Json rows_to_objects(Connection& c, const std::string& sql, const std::vector<Json>& params = {}) {
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(c.db, sql.c_str(), -1, &statement, nullptr) != SQLITE_OK) c.raise("prepare failed");
    std::vector<std::string> names;
    for (int i = 0; i < sqlite3_column_count(statement); ++i) names.emplace_back(sqlite3_column_name(statement, i));
    sqlite3_finalize(statement);
    Json::Array out;
    for (const auto& row : c.query(sql, params)) {
        Json::Object members;
        for (std::size_t i = 0; i < names.size(); ++i) members.emplace_back(names[i], row[i]);
        out.push_back(Json::object(std::move(members)));
    }
    return Json::array(std::move(out));
}

std::optional<UtcTimestamp> millis(const Json& value) {
    if (!value.is_string()) return std::nullopt;
    const auto parsed = parse_timestamp(value.as_string());
    if (!parsed.value) return std::nullopt;
    const auto micros = parsed.value->unix_micros;
    return UtcTimestamp::from_unix_millis(micros >= 0 ? micros / 1000 : -((-micros + 999) / 1000));
}

Json parsed_json(const Json& text) {
    if (!text.is_string()) return Json{};
    try {
        return parse_json(text.as_string());
    } catch (const JsonError&) {
        return Json{};
    }
}

}  // namespace

Json SqliteFinanceStore::snapshot(const SnapshotOptions& options) const {
    Connection c;
    c.db = db_;
    try {
        c.exec("BEGIN DEFERRED");  // one consistent read snapshot
        struct EndRead {
            sqlite3* db;
            ~EndRead() { sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr); }
        } end_read{db_};

        auto accounts = rows_to_objects(
            c,
            "SELECT a.*, s.source_kind AS source_kind, s.imported_at AS source_imported_at "
            "FROM accounts a LEFT JOIN sources s ON s.source_id = a.source_id AND s.source_version = a.source_version "
            "ORDER BY a.account_id");

        auto transactions = rows_to_objects(
            c,
            "SELECT r.transaction_id, r.account_id, r.source_transaction_id, r.revision, r.amount_minor, r.currency, "
            "r.direction, r.transaction_state, r.merchant_raw, r.merchant_id, r.description, r.category_id, "
            "r.authorized_at, r.posted_at, r.observed_at, r.source_id, r.source_version, r.user_note, r.user_tags_json, "
            "s.source_kind AS source_kind, s.imported_at AS source_imported_at, "
            "(SELECT COUNT(*) FROM transaction_revisions x WHERE x.account_id = r.account_id "
            " AND x.source_transaction_id = r.source_transaction_id) AS revision_count "
            "FROM transaction_revisions r JOIN transactions t ON t.account_id = r.account_id "
            "AND t.source_transaction_id = r.source_transaction_id AND t.current_revision = r.revision "
            "LEFT JOIN sources s ON s.source_id = r.source_id AND s.source_version = r.source_version "
            "ORDER BY COALESCE(r.posted_at, r.authorized_at, r.observed_at) DESC, r.transaction_id");

        // Map rows to the domain model for the core recognizers.
        std::vector<Transaction> domain;
        for (const auto& row : transactions.as_array()) {
            const auto currency = CurrencyCode::parse(row.get("currency").is_string() ? row.get("currency").as_string() : "");
            const auto observed = millis(row.get("observed_at"));
            if (!currency || !observed) continue;
            const auto text = [&](const char* key) { return python_str(row.get(key)).value_or(""); };
            const auto optional_text = [&](const char* key) -> std::optional<std::string> {
                return row.get(key).is_null() ? std::nullopt : python_str(row.get(key));
            };
            const auto state = text("transaction_state");
            domain.push_back(Transaction{
                .transaction_id = text("transaction_id"),
                .account_id = text("account_id"),
                .source_transaction_id = text("source_transaction_id"),
                .revision = static_cast<std::uint64_t>(row.get("revision").as_int()),
                .amount = Money{row.get("amount_minor").as_int(), *currency},
                .direction = text("direction") == "credit" ? TransactionDirection::credit : TransactionDirection::debit,
                .transaction_state = state == "posted"     ? TransactionState::posted
                                     : state == "pending"  ? TransactionState::pending
                                     : state == "reversed" ? TransactionState::reversed
                                     : state == "removed"  ? TransactionState::removed
                                                           : TransactionState::unknown,
                .merchant_raw = text("merchant_raw"),
                .merchant_id = optional_text("merchant_id"),
                .description = text("description"),
                .category_id = optional_text("category_id"),
                .authorized_at = millis(row.get("authorized_at")),
                .posted_at = millis(row.get("posted_at")),
                .observed_at = *observed,
                .recurring_candidate_id = std::nullopt,
                .source_ref = SourceRef{text("source_id"), SourceKind::local_api, std::nullopt, std::nullopt, *observed,
                                        *observed, "", static_cast<std::uint64_t>(row.get("source_version").as_int()), ""},
                .supersedes_transaction_revision = std::nullopt,
                .user_note = text("user_note"),
                .user_tags = {},
            });
        }
        std::map<std::string, std::pair<std::string, std::string>> classification;  // id -> (kind, related)
        for (const auto& match : recognize_transfers(domain, options.transfer_window_hours * 3'600'000)) {
            classification[match.outgoing_transaction_id] = {"transfer_out", match.incoming_transaction_id};
            classification[match.incoming_transaction_id] = {"transfer_in", match.outgoing_transaction_id};
        }
        for (const auto& match : recognize_refunds(domain, options.refund_window_days)) {
            if (classification.contains(match.refund_transaction_id) ||
                classification.contains(match.original_transaction_id)) {
                continue;  // a leg already explained as a transfer stays a transfer
            }
            classification[match.refund_transaction_id] = {"refund", match.original_transaction_id};
            classification[match.original_transaction_id] = {"refunded_purchase", match.refund_transaction_id};
        }
        for (auto& row : transactions.as_array()) {
            const auto id = python_str(row.get("transaction_id")).value_or("");
            const auto found = classification.find(id);
            const bool credit = row.get("direction") == Json::string("credit");
            row.set("classification", Json::string(found != classification.end() ? found->second.first
                                                   : credit                      ? "income"
                                                                                 : "spending"));
            row.set("related_transaction_id",
                    found != classification.end() ? Json::string(found->second.second) : Json{});
            const bool matchable = row.get("transaction_state") == Json::string("posted") &&
                                   (!row.get("posted_at").is_null() || !row.get("authorized_at").is_null());
            row.set("matchable", Json::boolean(matchable));
            row.set("user_tags", parsed_json(row.get("user_tags_json")));
            row.erase("user_tags_json");
        }

        auto commitments = rows_to_objects(c, "SELECT * FROM commitments ORDER BY next_expected_at, commitment_id");
        for (auto& row : commitments.as_array()) {
            row.set("amount_model", parsed_json(row.get("amount_model_json")));
            row.erase("amount_model_json");
        }
        auto budgets = rows_to_objects(c, "SELECT * FROM budgets ORDER BY budget_id");
        for (auto& row : budgets.as_array()) {
            row.set("allocations", rows_to_objects(c, "SELECT * FROM budget_allocations WHERE budget_id = ? "
                                                      "ORDER BY category_id", {row.get("budget_id")}));
        }
        auto goals = rows_to_objects(c, "SELECT * FROM goals ORDER BY priority, goal_id");
        for (auto& row : goals.as_array()) {
            Json::Array plan_accounts;
            for (const auto& plan : c.query("SELECT account_id FROM goal_plans WHERE goal_id = ? ORDER BY account_id",
                                            {row.get("goal_id")})) {
                plan_accounts.push_back(plan[0]);
            }
            row.set("funding_account_ids", Json::array(std::move(plan_accounts)));
        }
        auto receipts = rows_to_objects(c, "SELECT * FROM calculation_receipts ORDER BY calculated_at DESC, receipt_id "
                                           "LIMIT 50");
        for (auto& row : receipts.as_array()) {
            row.set("input_refs", parsed_json(row.get("input_refs_json")));
            row.set("outputs", parsed_json(row.get("outputs_json")));
            row.erase("input_refs_json");
            row.erase("outputs_json");
        }
        auto batches = rows_to_objects(c, "SELECT * FROM import_batches ORDER BY completed_at DESC, batch_id");
        for (auto& row : batches.as_array()) {
            row.set("warnings", parsed_json(row.get("warnings_json")));
            row.erase("warnings_json");
        }
        auto observations = rows_to_objects(
            c, "SELECT * FROM balance_observations ORDER BY observed_at DESC, observation_id LIMIT 200");
        auto sources = rows_to_objects(
            c, "SELECT source_id, source_kind, source_version, imported_at, observed_at, content_hash "
               "FROM sources ORDER BY imported_at DESC, source_id, source_version");

        Json::Array blockers;
        for (const char* table : {"connector_state", "merchants", "merchant_aliases", "category_assignments",
                                  "income_streams", "planned_events", "forecast_runs", "forecast_points", "insights",
                                  "scenarios", "settings"}) {
            if (c.one(std::string("SELECT COUNT(*) FROM ") + table)->at(0).as_int() != 0) {
                blockers.push_back(Json::string(table));
            }
        }
        const auto event_count = c.one("SELECT COUNT(*) FROM event_log")->at(0);
        return Json::object({
            {"schema", Json::string("bundle-snapshot-1")},
            {"accounts", accounts},
            {"transactions", transactions},
            {"balance_observations", observations},
            {"commitments", commitments},
            {"budgets", budgets},
            {"goals", goals},
            {"receipts", receipts},
            {"import_batches", batches},
            {"sources", sources},
            {"export_blockers", Json::array(std::move(blockers))},
            {"event_count", event_count},
            {"classification_rules",
             Json::object({{"engine", Json::string("bundle::recognize_transfers / recognize_refunds")},
                           {"transfer_window_hours", Json::integer(options.transfer_window_hours)},
                           {"refund_window_days", Json::integer(options.refund_window_days)},
                           {"requires", Json::string("state posted and a posted_at or authorized_at time")}})},
        });
    } catch (const SqliteError& error) {
        throw StoreError(std::string("snapshot query failed: ") + error.what());
    }
}

}  // namespace bundle::persistence
