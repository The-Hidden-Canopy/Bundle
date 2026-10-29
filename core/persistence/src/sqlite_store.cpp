// Native port of connectors/bundle_connectors/sqlite_store.py.
//
// Function names below match the Python methods they mirror so the two can be
// reviewed side by side. Deliberate differences are marked "DIVERGENCE" and
// listed in docs/native-sqlite-adapter.md.

#include "bundle/persistence/sqlite_store.hpp"

#include "bundle/persistence/archive.hpp"
#include "bundle/persistence/text.hpp"
#include "sqlite_db.hpp"

#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <tuple>

namespace bundle::persistence {
namespace fs = std::filesystem;
using detail::Connection;
using detail::Row;
using detail::SqliteError;
using detail::WriteTransaction;

namespace {

using Columns = std::vector<std::string>;

const std::set<std::string> k_account_types = {"checking", "savings", "cash", "credit_card",
                                               "prepaid", "investment", "loan", "other"};
const std::set<std::string> k_transaction_states = {"pending", "posted", "reversed", "removed", "unknown"};
const std::set<std::string> k_directions = {"debit", "credit"};
const std::set<std::string> k_balance_statuses = {"valid", "stale", "unavailable", "unknown"};
const std::set<std::string> k_account_freshness = {"fresh", "aging", "stale", "unavailable"};
const std::set<std::string> k_account_statuses = {"active", "closed", "unavailable", "unknown"};

const Columns k_source_columns = {"source_id", "source_kind", "provider_id", "external_object_id", "imported_at",
                                  "observed_at", "content_hash", "source_version", "metadata_json"};
const Columns k_account_columns = {
    "account_id", "source_id", "source_version", "display_name", "institution_name",
    "account_type", "account_subtype", "currency", "current_minor", "available_minor",
    "balance_observed_at", "balance_freshness", "last_four", "status",
    "include_in_safe_to_spend", "include_in_net_position"};
const Columns k_balance_columns = {"observation_id", "account_id", "current_minor", "available_minor",
                                   "observed_at", "recorded_at", "source_id", "source_version", "status"};
const Columns k_transaction_columns = {
    "transaction_id", "account_id", "source_transaction_id", "revision", "amount_minor",
    "currency", "direction", "transaction_state", "merchant_raw", "merchant_id",
    "description", "category_id", "authorized_at", "posted_at", "observed_at",
    "recurring_candidate_id", "source_id", "source_version",
    "supersedes_transaction_revision", "user_note", "user_tags_json"};
const Columns k_commitment_columns = {
    "commitment_id", "name", "merchant_id", "amount_model_json", "currency", "cadence",
    "next_expected_at", "grace_window_millis", "commitment_type", "status", "confidence",
    "user_confirmed", "created_at", "updated_at"};
const Columns k_commitment_evidence_columns = {"commitment_id", "source_id", "source_version",
                                               "related_transaction_id"};
const Columns k_budget_columns = {"budget_id", "name", "mode", "period_rule", "rollover_policy",
                                  "starts_at", "ends_at", "status"};
const Columns k_budget_allocation_columns = {"budget_id", "category_id", "limit_minor", "spent_minor",
                                             "pending_minor", "remaining_minor", "burn_rate_basis_points"};
const Columns k_goal_columns = {"goal_id", "name", "target_minor", "currency", "target_date",
                                "current_reserved_minor", "planned_contribution_minor", "priority",
                                "protected", "status"};
const Columns k_goal_plan_columns = {"goal_id", "account_id"};
const Columns k_override_columns = {"override_id", "object_type", "object_id", "field",
                                    "old_projection_json", "new_user_value_json", "created_at"};
const Columns k_receipt_columns = {"receipt_id", "engine", "engine_version", "calculated_at",
                                   "input_refs_json", "input_hash", "policy_id", "policy_version",
                                   "outputs_json"};
const std::array<const char*, 11> k_archive_unsupported_tables = {
    "connector_state", "merchants", "merchant_aliases", "category_assignments", "income_streams",
    "planned_events", "forecast_runs", "forecast_points", "insights", "scenarios", "settings"};

std::string join(const Columns& columns, const std::string& prefix = "") {
    std::string out;
    for (std::size_t i = 0; i < columns.size(); ++i) {
        if (i != 0) out += ", ";
        out += prefix + columns[i];
    }
    return out;
}

std::string placeholders(const std::size_t count) {
    std::string out;
    for (std::size_t i = 0; i < count; ++i) out += i == 0 ? "?" : ", ?";
    return out;
}

Json row_dict(const Row& row, const Columns& columns) {
    Json::Object members;
    for (std::size_t i = 0; i < columns.size(); ++i) members.emplace_back(columns[i], row[i]);
    return Json::object(std::move(members));
}

[[noreturn]] void fail(const std::string& message) { throw StoreError(message); }

std::string repr(const std::string& value) { return "'" + value + "'"; }

// str(value) for a scalar; containers have no stable persisted form.
std::string pystr(const Json& value, const std::string& label) {
    auto text = python_str(value);
    if (!text) fail(label + " must be a scalar value (native adapter rejects container values here)");
    return *text;
}

// DIVERGENCE (fail-closed): Python str.lower()/upper() are Unicode-aware; the
// native adapter folds ASCII only and rejects non-ASCII input in these fields.
std::string fold_ascii(const std::string& value, const bool upper, const std::string& label) {
    std::string out = value;
    for (auto& c : out) {
        const auto byte = static_cast<unsigned char>(c);
        if (byte >= 0x80) fail(label + " must be ASCII in the native adapter");
        if (upper && c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
        if (!upper && c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

std::string sha256_of(const Json& value) { return sha256_tagged(canonical_json(value)); }

bool is_int(const Json& value) { return value.is_int(); }  // excludes bool, like the Python checks

std::string required_text(const Json& value, const std::string& key, const std::string& label) {
    const auto* result = value.find(key);
    if (result == nullptr || !result->is_string() || result->as_string().empty()) {
        fail(label + "." + key + " is required");
    }
    return result->as_string();
}

std::string required_sha256(const Json& value, const std::string& label) {
    if (!value.is_string() || !is_sha256_digest(value.as_string())) fail(label + " must be a SHA-256 digest");
    return value.as_string();
}

// _timestamp()
std::optional<std::string> timestamp(const Json& value, const std::string& label, const bool optional = false) {
    if (value.is_null() && optional) return std::nullopt;
    if (!value.is_string() || value.as_string().empty()) fail(label + " must be an RFC3339 timestamp");
    const auto parsed = parse_timestamp(value.as_string());
    if (!parsed.value) {
        if (parsed.problem == TimestampProblem::naive) fail(label + " must include an explicit timezone");
        if (parsed.problem == TimestampProblem::out_of_range) fail(label + " is out of the supported date range");
        fail(label + " is not a valid timestamp");
    }
    return parsed.value->text;
}

std::int64_t micros_of(const std::string& normalized) { return parse_timestamp(normalized).value->unix_micros; }

struct Source final {
    std::string source_id;
    std::string source_kind;
    Json provider_id;
    Json external_object_id;
    std::string imported_at;
    std::string observed_at;
    std::string content_hash;
    std::int64_t source_version = 0;
    std::string metadata_json;

    friend bool operator==(const Source& a, const Source& b) {
        return a.source_id == b.source_id && a.source_kind == b.source_kind && a.provider_id == b.provider_id &&
               a.external_object_id == b.external_object_id && a.imported_at == b.imported_at &&
               a.observed_at == b.observed_at && a.content_hash == b.content_hash &&
               a.source_version == b.source_version && a.metadata_json == b.metadata_json;
    }
    [[nodiscard]] std::pair<std::string, std::int64_t> key() const { return {source_id, source_version}; }
};

// _source_ref()
Source source_ref(const Json& value, const std::string& label) {
    const auto* ref = value.find("source_ref");
    if (ref == nullptr || !ref->is_object()) fail(label + ".source_ref is required");
    Source source;
    source.source_id = required_text(*ref, "source_id", label + ".source_ref");
    source.content_hash = required_text(*ref, "content_hash", label + ".source_ref");
    if (!is_sha256_digest(source.content_hash)) fail(label + ".source_ref.content_hash must be a SHA-256 digest");
    source.source_kind = required_text(*ref, "source_kind", label + ".source_ref");
    const auto version = ref->get("source_version");
    if (!is_int(version) || version.as_int() < 1) fail(label + ".source_ref.source_version must be positive");
    source.source_version = version.as_int();
    source.imported_at = *timestamp(ref->get("imported_at"), label + ".source_ref.imported_at");
    source.observed_at = *timestamp(ref->get("observed_at"), label + ".source_ref.observed_at");
    source.provider_id = ref->get("provider_id");
    source.external_object_id = ref->get("external_object_id");
    // json.dumps(metadata, sort_keys=True, separators=(",", ":")) -- ASCII escaping
    source.metadata_json = dump_json(ref->get("metadata", Json::object()),
                                     DumpOptions{.sort_keys = true, .ensure_ascii = true});
    return source;
}

// _archive_source()
Source archive_source(const Json& value, const std::string& label) {
    if (!value.is_object()) fail(label + ".source must be an object");
    Json metadata;
    if (const auto* metadata_json = value.find("metadata_json"); metadata_json != nullptr) {
        if (!metadata_json->is_string()) fail(label + ".source.metadata_json must be JSON text");
        try {
            metadata = parse_json(metadata_json->as_string());
        } catch (const JsonError&) {
            fail(label + ".source.metadata_json is invalid JSON");
        }
    } else {
        metadata = value.get("metadata", Json::object());
    }
    if (!metadata.is_object()) fail(label + ".source metadata must contain an object");
    Json copy = value;
    copy.set("metadata", metadata);
    return source_ref(Json::object({{"source_ref", copy}}), label);
}

Json source_archive_record(const Source& source) {
    return Json::object({
        {"source_id", Json::string(source.source_id)},
        {"source_kind", Json::string(source.source_kind)},
        {"provider_id", source.provider_id},
        {"external_object_id", source.external_object_id},
        {"imported_at", Json::string(source.imported_at)},
        {"observed_at", Json::string(source.observed_at)},
        {"content_hash", Json::string(source.content_hash)},
        {"source_version", Json::integer(source.source_version)},
        {"metadata", parse_json(source.metadata_json)},
    });
}

// _archive_json_document()
Json archive_json_document(const Json& value, const std::string& label, const bool object_only = false) {
    if (!value.is_string()) fail(label + " must be JSON text");
    Json parsed;
    try {
        parsed = parse_json(value.as_string());
    } catch (const JsonError&) {
        fail(label + " is invalid JSON");
    }
    if (object_only && !parsed.is_object()) fail(label + " must contain an object");
    return parsed;
}

// _archive_json_text()
std::string archive_json_text(const Json& value, const std::string& label, const bool object_only = false) {
    if (object_only && !value.is_object()) fail(label + " must contain an object");
    return canonical_json(value);
}

bool is_string_list(const Json& value) {
    if (!value.is_array()) return false;
    return std::all_of(value.as_array().begin(), value.as_array().end(), [](const Json& v) { return v.is_string(); });
}

class Adapter final {
public:
    explicit Adapter(Connection& c) : c_(c) {}

    // _ensure_event()
    void ensure_event(const std::string& event_id, const std::string& event_type, const std::string& object_id,
                      const std::string& occurred_at, const std::string& content_hash) {
        const auto existing = c_.one(
            "SELECT event_type, object_id, occurred_at, content_hash FROM event_log WHERE event_id = ?",
            {Json::string(event_id)});
        const Row expected = {Json::string(event_type), Json::string(object_id), Json::string(occurred_at),
                              Json::string(content_hash)};
        if (existing) {
            if (*existing != expected) fail("event conflict for " + repr(event_id));
            return;
        }
        c_.run("INSERT INTO event_log (event_id, event_type, object_id, occurred_at, content_hash) "
               "VALUES (?, ?, ?, ?, ?)",
               {Json::string(event_id), Json::string(event_type), Json::string(object_id),
                Json::string(occurred_at), Json::string(content_hash)});
    }

    // _ensure_source()
    void ensure_source(const Source& source) {
        const auto existing = c_.one("SELECT content_hash FROM sources WHERE source_id = ? AND source_version = ?",
                                     {Json::string(source.source_id), Json::integer(source.source_version)});
        if (existing) {
            if ((*existing)[0] != Json::string(source.content_hash)) {
                fail("source conflict for " + repr(source.source_id) + " version " +
                     std::to_string(source.source_version));
            }
            return;
        }
        c_.run("INSERT INTO sources (source_id, source_kind, provider_id, external_object_id, imported_at, "
               "observed_at, content_hash, source_version, metadata_json) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)",
               {Json::string(source.source_id), Json::string(source.source_kind), source.provider_id,
                source.external_object_id, Json::string(source.imported_at), Json::string(source.observed_at),
                Json::string(source.content_hash), Json::integer(source.source_version),
                Json::string(source.metadata_json)});
    }

    // _ensure_account()
    void ensure_account(const Json& account, const Source& source) {
        const auto account_id = required_text(account, "account_id", "account");
        const auto currency = fold_ascii(required_text(account, "currency", "account " + account_id), true,
                                         "account " + account_id + ".currency");
        const auto account_type = fold_ascii(pystr(account.get("account_type", Json::string("other")),
                                                   "account " + account_id + ".account_type"),
                                             false, "account " + account_id + ".account_type");
        if (!k_account_types.contains(account_type)) fail("account " + repr(account_id) + " has unsupported account_type");
        const auto display_name = pystr(account.get("display_name", Json::string(account_id)), "account.display_name");
        const auto institution = pystr(account.get("institution_name", Json::string("")), "account.institution_name");
        const auto subtype = pystr(account.get("account_subtype", Json::string("")), "account.account_subtype");
        const auto existing = c_.one("SELECT source_id, source_version, currency FROM accounts WHERE account_id = ?",
                                     {Json::string(account_id)});
        if (existing) {
            const auto& [existing_source_id, existing_version, existing_currency] =
                std::tie((*existing)[0], (*existing)[1], (*existing)[2]);
            if (existing_source_id != Json::string(source.source_id) || existing_currency != Json::string(currency)) {
                fail("account conflict for " + repr(account_id));
            }
            if (existing_version.as_int() > source.source_version) {
                fail("account " + repr(account_id) + " received an older source version");
            }
            if (existing_version.as_int() == source.source_version) return;
            c_.run("UPDATE accounts SET source_id = ?, source_version = ?, display_name = ?, institution_name = ?, "
                   "account_type = ?, account_subtype = ?, currency = ?, last_four = ? WHERE account_id = ?",
                   {Json::string(source.source_id), Json::integer(source.source_version), Json::string(display_name),
                    Json::string(institution), Json::string(account_type), Json::string(subtype),
                    Json::string(currency), account.get("last_four"), Json::string(account_id)});
            ensure_event("account-source:" + account_id + ":" + source.source_id + ":" +
                             std::to_string(source.source_version),
                         "account.source_projection.updated", account_id, source.observed_at, source.content_hash);
            return;
        }
        c_.run("INSERT INTO accounts (" + join(k_account_columns) + ") VALUES (" + placeholders(16) + ")",
               {Json::string(account_id), Json::string(source.source_id), Json::integer(source.source_version),
                Json::string(display_name), Json::string(institution), Json::string(account_type),
                Json::string(subtype), Json::string(currency), Json::integer(0), Json::integer(0),
                Json::string(source.observed_at), Json::string("unavailable"), account.get("last_four"),
                Json::string("active"), Json::integer(1), Json::integer(1)});
        ensure_event("account:" + account_id, "account.appended", account_id, source.observed_at, source.content_hash);
    }

    // _append_balance()
    void append_balance(const Json& balance, const Source& source, const std::string& recorded_at) {
        const auto account_id = required_text(balance, "account_id", "balance");
        const auto account = c_.one(
            "SELECT currency, balance_observed_at, balance_freshness FROM accounts WHERE account_id = ?",
            {Json::string(account_id)});
        if (!account) fail("balance references unknown account " + repr(account_id));
        const auto current = balance.get("current_minor");
        const auto available = balance.get("available_minor", current);
        if (!is_int(current)) fail("balance.current_minor must be an integer");
        if (!is_int(available)) fail("balance.available_minor must be an integer");
        const auto observed_at = *timestamp(balance.get("observed_at"), "balance.observed_at");
        const auto status = fold_ascii(pystr(balance.get("status", Json::string("valid")), "balance.status"), false,
                                       "balance.status");
        if (!k_balance_statuses.contains(status)) fail("balance has unsupported status " + repr(status));
        const auto given_id = balance.get("observation_id");
        const auto observation_id = given_id.truthy()
                                        ? pystr(given_id, "balance.observation_id")
                                        : source.source_id + ":" + account_id + ":" + observed_at;
        const auto existing = c_.one("SELECT " + join(k_balance_columns) +
                                         " FROM balance_observations WHERE observation_id = ?",
                                     {Json::string(observation_id)});
        const Row expected = {Json::string(observation_id), Json::string(account_id), current, available,
                              Json::string(observed_at), Json::string(recorded_at), Json::string(source.source_id),
                              Json::integer(source.source_version), Json::string(status)};
        if (!existing) {
            c_.run("INSERT INTO balance_observations (" + join(k_balance_columns) + ") VALUES (" + placeholders(9) + ")",
                   expected);
        } else if (*existing != expected) {
            fail("balance conflict for " + repr(observation_id));
        }
        const std::string freshness = status == "stale" ? "stale" : status == "valid" ? "fresh" : "unavailable";
        const auto current_observed = *timestamp((*account)[1], "account " + account_id + ".balance_observed_at");
        const auto rank = [](const Json& value) {
            if (value == Json::string("fresh")) return 3;
            if (value == Json::string("stale")) return 2;
            return 1;  // "unavailable" and anything unranked
        };
        const auto observed_micros = micros_of(observed_at);
        const auto current_micros = micros_of(current_observed);
        const bool should_project =
            observed_micros > current_micros ||
            (observed_micros == current_micros && rank(Json::string(freshness)) >= rank((*account)[2]));
        if (should_project) {
            c_.run("UPDATE accounts SET current_minor = ?, available_minor = ?, balance_observed_at = ?, "
                   "balance_freshness = ?, status = ? WHERE account_id = ?",
                   {current, available, Json::string(observed_at), Json::string(freshness), Json::string("active"),
                    Json::string(account_id)});
            ensure_event("account-balance:" + account_id + ":" + observation_id, "account.balance_projection.updated",
                         account_id, observed_at, source.content_hash);
        }
    }

    // _append_transaction()
    void append_transaction(const Json& transaction, const Source& source) {
        const auto transaction_id = required_text(transaction, "transaction_id", "transaction");
        const auto label = "transaction " + transaction_id;
        const auto account_id = required_text(transaction, "account_id", label);
        const auto account = c_.one("SELECT currency FROM accounts WHERE account_id = ?", {Json::string(account_id)});
        if (!account) fail("transaction references unknown account " + repr(account_id));
        const auto currency = (*account)[0];
        const auto source_transaction_id =
            pystr(transaction.get("source_transaction_id", Json::string(transaction_id)), label + ".source_transaction_id");
        const auto revision = transaction.get("revision", Json::integer(1));
        if (!is_int(revision) || revision.as_int() < 1) fail("transaction " + repr(transaction_id) + " has invalid revision");
        // _minor_amount()
        const auto amount = transaction.get("amount");
        if (!amount.is_object()) fail(label + ".amount must be an object");
        const auto minor = amount.get("minor_units");
        if (!is_int(minor) || minor.as_int() < 0) fail(label + ".amount.minor_units must be a non-negative integer");
        if (amount.get("currency") != currency) fail(label + ".amount currency does not match account currency");
        const auto direction = fold_ascii(pystr(transaction.get("direction", Json::string("")), label + ".direction"),
                                          false, label + ".direction");
        const auto state = fold_ascii(pystr(transaction.get("transaction_state", Json::string("")), label + ".transaction_state"),
                                      false, label + ".transaction_state");
        if (!k_directions.contains(direction) || !k_transaction_states.contains(state)) {
            fail("transaction " + repr(transaction_id) + " has invalid direction or state");
        }
        const auto observed_at = *timestamp(transaction.get("observed_at"), label + ".observed_at");
        const auto authorized_at = timestamp(transaction.get("authorized_at"), label + ".authorized_at", true);
        const auto posted_at = timestamp(transaction.get("posted_at"), label + ".posted_at", true);
        const auto supersedes = transaction.get("supersedes_transaction_revision");
        // isinstance(True, int) holds in Python, so a bool passes this check there too.
        if (!supersedes.is_null() && (!(supersedes.is_int() || supersedes.is_bool()) || supersedes.as_int() < 1)) {
            fail("transaction " + repr(transaction_id) + " has invalid supersession");
        }
        const auto merchant_raw = pystr(transaction.get("merchant_raw", Json::string("")), label + ".merchant_raw");
        const auto description = pystr(transaction.get("description", Json::string("")), label + ".description");
        const auto user_note = pystr(transaction.get("user_note", Json::string("")), label + ".user_note");
        const auto user_tags = transaction.get("user_tags", Json::array());
        if (!is_string_list(user_tags)) fail("transaction " + repr(transaction_id) + ".user_tags must be a string list");
        const auto user_tags_json = dump_json(user_tags, DumpOptions{.sort_keys = false, .ensure_ascii = true});
        const auto text_or_null = [](const std::optional<std::string>& v) { return v ? Json::string(*v) : Json{}; };
        const Row expected = {
            Json::string(transaction_id), Json::string(account_id), Json::string(source_transaction_id), revision,
            minor, currency, Json::string(direction), Json::string(state), Json::string(merchant_raw),
            transaction.get("merchant_id"), Json::string(description), transaction.get("category_id"),
            text_or_null(authorized_at), text_or_null(posted_at), Json::string(observed_at),
            transaction.get("recurring_candidate_id"), Json::string(source.source_id),
            Json::integer(source.source_version), supersedes, Json::string(user_note), Json::string(user_tags_json)};
        const auto existing = c_.one("SELECT " + join(k_transaction_columns) +
                                         " FROM transaction_revisions WHERE transaction_id = ?",
                                     {Json::string(transaction_id)});
        if (existing) {
            if (*existing != expected) fail("transaction conflict for " + repr(transaction_id));
            return;
        }
        const auto current = c_.one(
            "SELECT current_revision FROM transactions WHERE account_id = ? AND source_transaction_id = ?",
            {Json::string(account_id), Json::string(source_transaction_id)});
        if (!current) {
            if (revision.as_int() != 1 || !supersedes.is_null()) {
                fail("transaction " + repr(transaction_id) + " does not start at revision 1");
            }
            c_.run("INSERT INTO transactions (account_id, source_transaction_id, current_revision) VALUES (?, ?, ?)",
                   {Json::string(account_id), Json::string(source_transaction_id), revision});
        } else if (revision.as_int() != (*current)[0].as_int() + 1 || supersedes != (*current)[0]) {
            fail("transaction " + repr(transaction_id) + " has a revision gap");
        }
        c_.run("INSERT INTO transaction_revisions (" + join(k_transaction_columns) + ") VALUES (" +
                   placeholders(21) + ")",
               expected);
        if (current) {
            c_.run("UPDATE transactions SET current_revision = ? WHERE account_id = ? AND source_transaction_id = ?",
                   {revision, Json::string(account_id), Json::string(source_transaction_id)});
            ensure_event("transaction-current:" + account_id + ":" + source_transaction_id + ":" +
                             std::to_string(revision.as_int()),
                         "transaction.current_revision.updated", source_transaction_id, observed_at,
                         source.content_hash);
        }
    }

    // _insert_or_verify()
    // DIVERGENCE (reference finding A): keys are matched with SQL `IS` so a NULL
    // key column (commitment_evidence.related_transaction_id) is found again on a
    // repeated restore. The Python bridge uses `=`, never matches NULL, and inserts
    // a duplicate row every time the same archive is restored.
    bool insert_or_verify(const std::string& table, const Columns& columns, const Columns& key_columns,
                          const Json& record, const std::string& label) {
        for (const auto& column : columns) {
            if (!record.contains(column)) fail(label + " is missing a required persisted field");
        }
        std::string where;
        std::vector<Json> key_values;
        for (const auto& column : key_columns) {
            where += (where.empty() ? "" : " AND ") + column + " IS ?";
            key_values.push_back(record.get(column));
        }
        const auto existing = c_.one("SELECT " + join(columns) + " FROM " + table + " WHERE " + where, key_values);
        Row values;
        for (const auto& column : columns) values.push_back(record.get(column));
        if (existing) {
            if (*existing != values) fail(label + " conflicts with existing local data");
            return false;
        }
        c_.run("INSERT INTO " + table + " (" + join(columns) + ") VALUES (" + placeholders(columns.size()) + ")",
               values);
        return true;
    }

    void require_ints(const Json& record, std::initializer_list<const char*> fields, const std::string& label) {
        for (const auto* field : fields) {
            if (!is_int(record.get(field))) fail(label + "." + field + " must be an integer");
        }
    }

    // _restore_account()
    void restore_account(const Json& record, const Source& source) {
        const auto account_id = required_text(record, "account_id", "archive account");
        const auto label = "archive account " + repr(account_id);
        if (record.get("source_id") != Json::string(source.source_id) ||
            record.get("source_version") != Json::integer(source.source_version)) {
            fail(label + " has a source mismatch");
        }
        const auto currency = fold_ascii(required_text(record, "currency", "archive account " + account_id), true, label);
        const auto account_type = fold_ascii(pystr(record.get("account_type", Json::string("")), label), false, label);
        if (!k_account_types.contains(account_type)) fail(label + " has an invalid account type");
        const auto freshness = record.get("balance_freshness");
        if (!freshness.is_string() || !k_account_freshness.contains(freshness.as_string())) fail(label + " has invalid freshness");
        const auto status = record.get("status");
        if (!status.is_string() || !k_account_statuses.contains(status.as_string())) fail(label + " has invalid status");
        const auto last_four = record.get("last_four");
        if (!last_four.is_null()) {
            const bool ok = last_four.is_string() && last_four.as_string().size() == 4 &&
                            last_four.as_string().find_first_not_of("0123456789") == std::string::npos;
            if (!ok) fail(label + " has invalid last_four");
        }
        require_ints(record, {"source_version", "current_minor", "available_minor", "include_in_safe_to_spend",
                              "include_in_net_position"}, label);
        (void)timestamp(record.get("balance_observed_at"), "archive account " + account_id + ".balance_observed_at");
        Json values = record;
        values.set("currency", Json::string(currency));
        values.set("account_type", Json::string(account_type));
        if (insert_or_verify("accounts", k_account_columns, {"account_id"}, values, label)) {
            ensure_event("account:" + account_id, "account.appended", account_id,
                         pystr(record.get("balance_observed_at"), label), source.content_hash);
        }
    }

    // _restore_balance()
    void restore_balance(const Json& record, const Source& source) {
        const auto observation_id = required_text(record, "observation_id", "archive balance");
        const auto label = "archive balance " + repr(observation_id);
        if (record.get("source_id") != Json::string(source.source_id) ||
            record.get("source_version") != Json::integer(source.source_version)) {
            fail(label + " has a source mismatch");
        }
        require_ints(record, {"current_minor", "available_minor", "source_version"}, label);
        const auto status = fold_ascii(pystr(record.get("status", Json::string("")), label), false, label);
        if (!k_balance_statuses.contains(status)) fail(label + " has invalid status");
        (void)timestamp(record.get("observed_at"), "archive balance " + observation_id + ".observed_at");
        (void)timestamp(record.get("recorded_at"), "archive balance " + observation_id + ".recorded_at");
        Json values = record;
        values.set("status", Json::string(status));
        insert_or_verify("balance_observations", k_balance_columns, {"observation_id"}, values, label);
    }

    // _restore_commitment()
    void restore_commitment(const Json& record) {
        const auto commitment_id = required_text(record, "commitment_id", "archive commitment");
        const auto label = "archive commitment " + repr(commitment_id);
        const auto evidence = record.get("evidence", Json::array());
        if (!evidence.is_array() || !std::all_of(evidence.as_array().begin(), evidence.as_array().end(),
                                                 [](const Json& v) { return v.is_object(); })) {
            fail(label + ".evidence must be a list of objects");
        }
        for (const auto* field : {"name", "currency", "cadence", "commitment_type", "status", "confidence"}) {
            required_text(record, field, label);
        }
        require_ints(record, {"grace_window_millis", "user_confirmed"}, label);
        if (record.get("user_confirmed").as_int() != 0 && record.get("user_confirmed").as_int() != 1) {
            fail(label + ".user_confirmed is invalid");
        }
        for (const auto* field : {"next_expected_at", "created_at", "updated_at"}) {
            (void)timestamp(record.get(field), "archive commitment " + commitment_id + "." + field);
        }
        Json values = record;
        values.set("amount_model_json",
                   Json::string(archive_json_text(record.get("amount_model"),
                                                  "archive commitment " + commitment_id + ".amount_model", true)));
        values.erase("amount_model");
        values.erase("evidence");
        insert_or_verify("commitments", k_commitment_columns, {"commitment_id"}, values, label);
        ensure_event("commitment:" + commitment_id, "commitment.appended", commitment_id,
                     pystr(record.get("updated_at"), label), sha256_of(record));
        std::size_t index = 0;
        for (const auto& evidence_record : evidence.as_array()) {
            const auto evidence_label = label + ".evidence[" + std::to_string(index++) + "]";
            const auto source = archive_source(evidence_record.get("source"), evidence_label);
            const auto source_id = required_text(evidence_record, "source_id", evidence_label);
            const auto version = evidence_record.get("source_version");
            if (!is_int(version) || version.as_int() < 1) fail(evidence_label + ".source_version must be positive");
            if (source_id != source.source_id || version.as_int() != source.source_version) {
                fail(evidence_label + " has a source mismatch");
            }
            const auto related = evidence_record.get("related_transaction_id");
            if (!related.is_null() && !related.is_string()) fail(evidence_label + ".related_transaction_id must be text or null");
            insert_or_verify("commitment_evidence", k_commitment_evidence_columns, k_commitment_evidence_columns,
                             Json::object({{"commitment_id", Json::string(commitment_id)},
                                           {"source_id", Json::string(source_id)},
                                           {"source_version", version},
                                           {"related_transaction_id", related}}),
                             evidence_label);
            ensure_event("commitment-evidence:" + commitment_id + ":" + source_id + ":" +
                             std::to_string(version.as_int()) + ":" + (related.truthy() ? related.as_string() : ""),
                         "commitment_evidence.appended", commitment_id, source.observed_at, sha256_of(evidence_record));
        }
    }

    // _restore_budget()
    void restore_budget(const Json& record) {
        const auto budget_id = required_text(record, "budget_id", "archive budget");
        const auto label = "archive budget " + repr(budget_id);
        const auto allocations = record.get("allocations", Json::array());
        if (!allocations.is_array() || !std::all_of(allocations.as_array().begin(), allocations.as_array().end(),
                                                    [](const Json& v) { return v.is_object(); })) {
            fail(label + ".allocations must be a list of objects");
        }
        for (const auto* field : {"name", "mode", "period_rule", "rollover_policy", "status"}) required_text(record, field, label);
        (void)timestamp(record.get("starts_at"), "archive budget " + budget_id + ".starts_at");
        (void)timestamp(record.get("ends_at"), "archive budget " + budget_id + ".ends_at", true);
        Json values = record;
        values.erase("allocations");
        insert_or_verify("budgets", k_budget_columns, {"budget_id"}, values, label);
        const auto starts_at = pystr(record.get("starts_at"), label);
        ensure_event("budget:" + budget_id, "budget.appended", budget_id, starts_at, sha256_of(record));
        std::size_t index = 0;
        for (const auto& allocation : allocations.as_array()) {
            const auto allocation_label = label + ".allocations[" + std::to_string(index++) + "]";
            if (allocation.get("budget_id") != Json::string(budget_id)) fail(allocation_label + " has a budget mismatch");
            const auto category_id = required_text(allocation, "category_id", allocation_label);
            require_ints(allocation, {"limit_minor", "spent_minor", "pending_minor", "remaining_minor",
                                      "burn_rate_basis_points"}, allocation_label);
            Json::Object members;
            for (const auto& column : k_budget_allocation_columns) members.emplace_back(column, allocation.get(column));
            insert_or_verify("budget_allocations", k_budget_allocation_columns, {"budget_id", "category_id"},
                             Json::object(std::move(members)), allocation_label);
            ensure_event("budget-allocation:" + budget_id + ":" + category_id, "budget_allocation.appended", budget_id,
                         starts_at, sha256_of(allocation));
        }
    }

    // _restore_goal()
    void restore_goal(const Json& record) {
        const auto goal_id = required_text(record, "goal_id", "archive goal");
        const auto label = "archive goal " + repr(goal_id);
        const auto plans = record.get("plans", Json::array());
        if (!plans.is_array() || !std::all_of(plans.as_array().begin(), plans.as_array().end(),
                                              [](const Json& v) { return v.is_object(); })) {
            fail(label + ".plans must be a list of objects");
        }
        for (const auto* field : {"name", "currency", "status"}) required_text(record, field, label);
        require_ints(record, {"target_minor", "current_reserved_minor", "planned_contribution_minor", "priority",
                              "protected"}, label);
        for (const auto* field : {"target_minor", "current_reserved_minor", "planned_contribution_minor"}) {
            if (record.get(field).as_int() < 0) fail(label + " contains a negative amount");
        }
        if (record.get("protected").as_int() != 0 && record.get("protected").as_int() != 1) fail(label + ".protected is invalid");
        (void)timestamp(record.get("target_date"), "archive goal " + goal_id + ".target_date", true);
        Json values = record;
        values.erase("plans");
        insert_or_verify("goals", k_goal_columns, {"goal_id"}, values, label);
        const auto target = record.get("target_date");
        const auto occurred = target.truthy() ? pystr(target, label) : std::string("1970-01-01T00:00:00Z");
        ensure_event("goal:" + goal_id, "goal.appended", goal_id, occurred, sha256_of(record));
        std::size_t index = 0;
        for (const auto& plan : plans.as_array()) {
            const auto plan_label = label + ".plans[" + std::to_string(index++) + "]";
            if (plan.get("goal_id") != Json::string(goal_id)) fail(plan_label + " has a goal mismatch");
            const auto account_id = required_text(plan, "account_id", plan_label);
            insert_or_verify("goal_plans", k_goal_plan_columns, {"goal_id", "account_id"},
                             Json::object({{"goal_id", Json::string(goal_id)}, {"account_id", Json::string(account_id)}}),
                             plan_label);
            ensure_event("goal-plan:" + goal_id + ":" + account_id, "goal_plan.appended", goal_id, occurred,
                         sha256_of(plan));
        }
    }

    // _restore_override()
    void restore_override(const Json& record) {
        const auto override_id = required_text(record, "override_id", "archive override");
        const auto label = "archive override " + repr(override_id);
        for (const auto* field : {"object_type", "object_id", "field"}) required_text(record, field, label);
        (void)timestamp(record.get("created_at"), "archive override " + override_id + ".created_at");
        Json values = record;
        values.set("old_projection_json", Json::string(archive_json_text(record.get("old_projection"), label)));
        values.set("new_user_value_json", Json::string(archive_json_text(record.get("new_user_value"), label)));
        values.erase("old_projection");
        values.erase("new_user_value");
        insert_or_verify("user_overrides", k_override_columns, {"override_id"}, values, label);
        ensure_event("override:" + override_id, "user_override.appended", override_id,
                     pystr(record.get("created_at"), label), sha256_of(record));
    }

    // _restore_receipt()
    void restore_receipt(const Json& record) {
        const auto receipt_id = required_text(record, "receipt_id", "archive receipt");
        const auto label = "archive receipt " + repr(receipt_id);
        for (const auto* field : {"engine", "engine_version", "policy_id", "policy_version"}) required_text(record, field, label);
        (void)timestamp(record.get("calculated_at"), "archive receipt " + receipt_id + ".calculated_at");
        if (!is_string_list(record.get("input_refs"))) fail(label + ".input_refs must be a string list");
        const auto input_hash = required_sha256(record.get("input_hash"), "archive receipt " + receipt_id + ".input_hash");
        Json values = record;
        values.set("input_refs_json", Json::string(archive_json_text(record.get("input_refs"), label)));
        values.set("outputs_json", Json::string(archive_json_text(record.get("outputs"), label, true)));
        values.set("input_hash", Json::string(input_hash));
        values.erase("input_refs");
        values.erase("outputs");
        insert_or_verify("calculation_receipts", k_receipt_columns, {"receipt_id"}, values, label);
        ensure_event("calculation-receipt:" + receipt_id, "calculation_receipt.appended", receipt_id,
                     pystr(record.get("calculated_at"), label), sha256_of(record));
    }

    // _source_rows()
    std::map<std::pair<std::string, std::int64_t>, Json> source_rows() {
        std::map<std::pair<std::string, std::int64_t>, Json> output;
        for (const auto& row : c_.query("SELECT " + join(k_source_columns) + " FROM sources")) {
            const auto source = archive_source(row_dict(row, k_source_columns), "stored source");
            output[{pystr(row[0], "stored source"), row[7].as_int()}] = source_archive_record(source);
        }
        return output;
    }

private:
    Connection& c_;
};

// ConnectorBatch.__post_init__ + bundle-connector-1 shape.
std::size_t code_points(const std::string& text) {
    std::size_t count = 0;
    for (const char c : text) count += (static_cast<unsigned char>(c) & 0xC0) != 0x80;
    return count;
}

}  // namespace

void validate_connector_batch(const Json& batch) {
    static const std::set<std::string> required = {"schema", "connector_id", "started_at", "completed_at",
                                                   "accounts", "balances", "transactions",
                                                   "external_commitments", "next_cursor", "warnings"};
    if (!batch.is_object()) fail("connector batch must be a JSON object");
    std::set<std::string> present;
    for (const auto& [key, value] : batch.as_object()) {
        if (!required.contains(key)) fail("connector batch has unexpected field " + repr(key));
        present.insert(key);
    }
    for (const auto& key : required) {
        if (!present.contains(key)) fail("connector batch is missing field " + repr(key));
    }
    if (batch.get("schema") != Json::string("bundle-connector-1")) fail("connector batch schema must be bundle-connector-1");
    const auto connector_id = batch.get("connector_id");
    if (!connector_id.is_string() || connector_id.as_string().empty()) fail("connector id is required");
    const auto started = timestamp(batch.get("started_at"), "connector batch started_at");
    const auto completed = timestamp(batch.get("completed_at"), "connector batch completed_at");
    if (micros_of(*completed) < micros_of(*started)) fail("completed_at cannot precede started_at");
    const auto cursor = batch.get("next_cursor");
    if (!cursor.is_null() && (!cursor.is_string() || code_points(cursor.as_string()) > 4096)) {
        fail("cursor exceeds the bounded connector contract");
    }
    const auto warnings = batch.get("warnings");
    if (!warnings.is_array()) fail("connector warnings must be a list");
    for (const auto& warning : warnings.as_array()) {
        if (!warning.is_string() || code_points(warning.as_string()) > 4096) {
            fail("connector warnings must be bounded strings");
        }
    }
    static constexpr std::array<const char*, 8> secrets = {"password", "secret", "credential", "access_token",
                                                           "auth_token", "refresh_token", "api_key", "api-key"};
    for (const auto* field : {"accounts", "balances", "transactions", "external_commitments"}) {
        const auto values = batch.get(field);
        if (!values.is_array()) fail(std::string(field) + " must be a list");
        if (values.as_array().size() > 1'000'000) fail(std::string(field) + " exceeds the batch bound");
        for (const auto& value : values.as_array()) {
            if (!value.is_object()) fail(std::string(field) + " must be an object");
            for (const auto& [key, member] : value.as_object()) {
                if (code_points(key) > 128) fail(std::string(field) + " contains an invalid field name");
                std::string lowered = key;
                for (auto& c : lowered) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
                for (const auto* secret : secrets) {
                    if (lowered.find(secret) != std::string::npos) {
                        fail(std::string(field) + " contains a forbidden secret-looking field");
                    }
                }
            }
        }
    }
}

// -- store ----------------------------------------------------------------------

namespace {

Connection connection_of(sqlite3* db, std::function<void(const std::string&)>* hook) {
    Connection c;
    c.db = db;
    c.hook = hook;
    return c;
}

}  // namespace

SqliteFinanceStore::SqliteFinanceStore(const fs::path& database, std::optional<std::string> migration_sql) {
    const auto name = database.filename().string();
    if (name.empty() || name == "." || name == "..") fail("database path must name a local file");
    if (sqlite3_open_v2(database.string().c_str(), &db_, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) !=
        SQLITE_OK) {
        const std::string message = db_ != nullptr ? sqlite3_errmsg(db_) : "out of memory";
        sqlite3_close(db_);
        db_ = nullptr;
        fail("could not open database: " + message);
    }
    try {
        auto c = connection_of(db_, &hook_);
        sqlite3_busy_timeout(db_, 5000);
        c.exec("PRAGMA foreign_keys = ON");
        // _validate_persisted_hashes(), run read-only BEFORE migrating. The Python
        // bridge migrates first and validates after, so a refused legacy database
        // has already been altered (tables, triggers, WAL mode) when it refuses.
        // The verdict is the same either way because the migration never rewrites
        // existing rows; validating first just leaves a refused file untouched.
        const auto table_exists = [&](const char* table) {
            return c.one("SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = ?", {Json::string(table)})
                .has_value();
        };
        if (table_exists("sources")) {
            for (const auto& row : c.query("SELECT " + join(k_source_columns) + " FROM sources")) {
                (void)archive_source(row_dict(row, k_source_columns), "stored source");
            }
        }
        const std::array<std::tuple<const char*, const char*, const char*>, 3> hashed = {{
            {"event_log", "content_hash", "stored event"},
            {"forecast_runs", "input_hash", "stored forecast run"},
            {"calculation_receipts", "input_hash", "stored calculation receipt"},
        }};
        for (const auto& [table, column, label] : hashed) {
            if (!table_exists(table)) continue;
            for (const auto& row : c.query(std::string("SELECT ") + column + " FROM " + table)) {
                (void)required_sha256(row[0], std::string(label) + "." + column);
            }
        }
        // Apply the schema statements atomically (executescript in Python commits
        // statement by statement). Leading PRAGMA statements -- journal_mode cannot
        // change inside a transaction -- run after the schema commits.
        const auto& script = migration_sql ? *migration_sql : embedded_migration();
        std::string pragmas;
        std::size_t body_start = 0;
        for (;;) {
            const auto line_end = script.find('\n', body_start);
            const auto line = script.substr(body_start, line_end == std::string::npos ? std::string::npos
                                                                                     : line_end - body_start);
            const auto first = line.find_first_not_of(" \t\r");
            if (first != std::string::npos && line.compare(first, 6, "PRAGMA") != 0) break;
            pragmas += line + "\n";
            if (line_end == std::string::npos) {
                body_start = script.size();
                break;
            }
            body_start = line_end + 1;
        }
        {
            WriteTransaction migration(c);
            c.exec(script.substr(body_start));
            migration.commit();
        }
        if (!pragmas.empty()) c.exec(pragmas);
    } catch (const SqliteError& error) {
        sqlite3_close(db_);
        db_ = nullptr;
        fail(std::string("database migration failed: ") + error.what());
    } catch (...) {
        sqlite3_close(db_);
        db_ = nullptr;
        throw;
    }
}

SqliteFinanceStore::~SqliteFinanceStore() {
    if (db_ != nullptr) sqlite3_close(db_);
}

SqliteFinanceStore::BatchPreview SqliteFinanceStore::preview_batch(const Json& batch) {
    preview_only_ = true;
    struct Reset {
        bool& flag;
        ~Reset() { flag = false; }
    } reset{preview_only_};
    auto c = connection_of(db_, nullptr);
    const auto before = c.one("SELECT COUNT(*) FROM event_log")->at(0).as_int();
    std::int64_t during = before;
    preview_counter_ = &during;
    struct ResetCounter {
        std::int64_t*& counter;
        ~ResetCounter() { counter = nullptr; }
    } reset_counter{preview_counter_};
    const auto batch_id = append_batch(batch);
    const bool admitted = c.one("SELECT 1 FROM import_batches WHERE batch_id = ?", {Json::string(batch_id)}).has_value();
    return BatchPreview{batch_id, admitted, during - before};
}

std::string SqliteFinanceStore::append_batch(const Json& batch) {
    validate_connector_batch(batch);
    if (!batch.get("external_commitments").as_array().empty()) {
        fail("external commitments require explicit planning admission and are not silently discarded");
    }
    const auto connector_id = batch.get("connector_id").as_string();
    const auto started_at = *timestamp(batch.get("started_at"), "started_at");
    const auto completed_at = *timestamp(batch.get("completed_at"), "completed_at");
    Json payload = batch;  // ConnectorBatch.to_dict(): timestamps normalized to UTC text
    payload.set("started_at", Json::string(started_at));
    payload.set("completed_at", Json::string(completed_at));
    const auto batch_hash = sha256_of(payload);
    const auto batch_id = connector_id + ":" + batch_hash.substr(7);

    const auto accounts = batch.get("accounts").as_array();
    const auto balances = batch.get("balances").as_array();
    const auto transactions = batch.get("transactions").as_array();
    std::vector<Json> candidates(transactions.begin(), transactions.end());
    candidates.insert(candidates.end(), balances.begin(), balances.end());
    std::vector<std::pair<std::string, std::int64_t>> account_candidates;
    for (std::size_t index = 0; index < accounts.size(); ++index) {
        const auto& account = accounts[index];
        const auto source_id = account.get("source_id");
        if (source_id.is_null()) {
            if (account.contains("source_version")) fail("account " + std::to_string(index) + ".source_version requires source_id");
            continue;
        }
        if (!source_id.is_string() || source_id.as_string().empty()) {
            fail("account " + std::to_string(index) + ".source_id must be a non-empty string");
        }
        const auto version = account.get("source_version", Json::integer(1));
        if (!is_int(version) || version.as_int() < 1) fail("account " + std::to_string(index) + ".source_version must be positive");
        account_candidates.emplace_back(source_id.as_string(), version.as_int());
    }

    std::optional<Source> source;
    if (!candidates.empty()) {
        source = source_ref(candidates.front(), "batch record");
    } else if (!accounts.empty()) {
        source = Source{batch_id, connector_id, Json{}, Json{}, started_at, completed_at, batch_hash, 1, "{}"};
    }
    if (source && candidates.empty() && !account_candidates.empty()) {
        std::tie(source->source_id, source->source_version) = account_candidates.front();
    }
    if (source) {
        // The durable source identity represents the complete admitted batch.
        source->content_hash = batch_hash;
        for (std::size_t index = 0; index < candidates.size(); ++index) {
            const auto record_source = source_ref(candidates[index], "batch record " + std::to_string(index));
            if (record_source.key() != source->key()) fail("connector batch contains mixed source identities");
        }
        for (const auto& candidate : account_candidates) {
            if (candidate != source->key()) fail("connector batch contains mixed account source identities");
        }
    }

    auto c = connection_of(db_, &hook_);
    try {
        WriteTransaction transaction(c);
        Adapter adapter(c);
        if (source) {
            adapter.ensure_source(*source);
            for (const auto& account : accounts) adapter.ensure_account(account, *source);
            for (const auto& balance : balances) adapter.append_balance(balance, *source, completed_at);
            for (const auto& record : transactions) adapter.append_transaction(record, *source);
        }
        c.run("INSERT INTO import_batches (batch_id, connector_id, started_at, completed_at, next_cursor, warnings_json) "
              "VALUES (?, ?, ?, ?, ?, ?) ON CONFLICT(batch_id) DO NOTHING",
              {Json::string(batch_id), Json::string(connector_id), Json::string(started_at), Json::string(completed_at),
               batch.get("next_cursor"),
               Json::string(dump_json(batch.get("warnings"), DumpOptions{.sort_keys = false, .ensure_ascii = true}))});
        adapter.ensure_event("import-batch:" + batch_id, "import_batch.admitted", batch_id, completed_at, batch_hash);
        if (preview_only_) {
            *preview_counter_ = c.one("SELECT COUNT(*) FROM event_log")->at(0).as_int();
            return batch_id;  // WriteTransaction rolls back on scope exit
        }
        transaction.commit();
    } catch (const SqliteError& error) {
        fail("SQLite admission rejected batch " + repr(batch_id) + ": " + error.what());
    }
    return batch_id;
}

void SqliteFinanceStore::export_archive(const fs::path& destination, const std::string& created_at) {
    const auto created = timestamp(Json::string(created_at), "export created_at");
    auto c = connection_of(db_, &hook_);
    ArchiveRecords records;
    try {
        // A read transaction gives the export one consistent snapshot.
        c.exec("BEGIN DEFERRED");
        struct EndRead {
            Connection& c;
            ~EndRead() { sqlite3_exec(c.db, "COMMIT", nullptr, nullptr, nullptr); }
        } end_read{c};
        for (const auto* table : k_archive_unsupported_tables) {
            if (c.one(std::string("SELECT COUNT(*) FROM ") + table)->at(0).as_int() != 0) {
                fail(std::string("bundle-finance-1 archive cannot represent non-empty table ") + repr(table));
            }
        }
        Adapter adapter(c);
        const auto sources = adapter.source_rows();
        std::set<std::pair<std::string, std::int64_t>> referenced;
        const auto attach = [&](Json& record, const std::string& label) {
            const std::pair<std::string, std::int64_t> key{pystr(record.get("source_id"), label),
                                                           record.get("source_version").as_int()};
            const auto it = sources.find(key);
            if (it == sources.end()) {
                fail(label + " references an unknown source ('" + key.first + "', " + std::to_string(key.second) + ")");
            }
            record.set("source", it->second);
            referenced.insert(key);
        };
        for (const auto& row : c.query("SELECT " + join(k_account_columns) + " FROM accounts ORDER BY account_id")) {
            auto record = row_dict(row, k_account_columns);
            attach(record, "account");
            records.accounts.push_back(std::move(record));
        }
        for (const auto& row : c.query("SELECT " + join(k_balance_columns) +
                                       " FROM balance_observations ORDER BY observation_id")) {
            auto record = row_dict(row, k_balance_columns);
            attach(record, "balance");
            records.balances.push_back(std::move(record));
        }
        for (const auto& row : c.query("SELECT " + join(k_transaction_columns, "r.") +
                                       ", t.current_revision FROM transaction_revisions AS r JOIN transactions AS t "
                                       "ON t.account_id = r.account_id AND t.source_transaction_id = "
                                       "r.source_transaction_id ORDER BY r.account_id, r.source_transaction_id, "
                                       "r.revision")) {
            auto record = row_dict(Row(row.begin(), row.end() - 1), k_transaction_columns);
            record.set("current_revision", row.back());
            const auto id = pystr(record.get("transaction_id"), "transaction");
            Json tags;
            try {
                tags = parse_json(record.get("user_tags_json").as_string());
            } catch (const std::exception&) {
                fail("transaction " + repr(id) + " has invalid user tags JSON");
            }
            if (!is_string_list(tags)) fail("transaction " + repr(id) + " has invalid user tags");
            record.erase("user_tags_json");
            record.set("user_tags", tags);
            attach(record, "transaction");
            records.transactions.push_back(std::move(record));
        }
        std::map<std::string, Json::Array> evidence_by_commitment;
        for (const auto& row : c.query("SELECT " + join(k_commitment_evidence_columns) +
                                       " FROM commitment_evidence ORDER BY commitment_id, source_id, "
                                       "source_version, related_transaction_id")) {
            auto record = row_dict(row, k_commitment_evidence_columns);
            attach(record, "commitment evidence");
            evidence_by_commitment[pystr(record.get("commitment_id"), "commitment evidence")].push_back(std::move(record));
        }
        for (const auto& row : c.query("SELECT " + join(k_commitment_columns) + " FROM commitments ORDER BY commitment_id")) {
            auto record = row_dict(row, k_commitment_columns);
            const auto id = pystr(record.get("commitment_id"), "commitment");
            record.set("amount_model", archive_json_document(record.get("amount_model_json"),
                                                             "commitment " + id + ".amount_model_json", true));
            record.erase("amount_model_json");
            record.set("evidence", Json::array(evidence_by_commitment[id]));
            records.commitments.push_back(std::move(record));
        }
        std::map<std::string, Json::Array> allocations;
        for (const auto& row : c.query("SELECT " + join(k_budget_allocation_columns) +
                                       " FROM budget_allocations ORDER BY budget_id, category_id")) {
            auto record = row_dict(row, k_budget_allocation_columns);
            allocations[pystr(record.get("budget_id"), "allocation")].push_back(std::move(record));
        }
        for (const auto& row : c.query("SELECT " + join(k_budget_columns) + " FROM budgets ORDER BY budget_id")) {
            auto record = row_dict(row, k_budget_columns);
            record.set("allocations", Json::array(allocations[pystr(record.get("budget_id"), "budget")]));
            records.budgets.push_back(std::move(record));
        }
        std::map<std::string, Json::Array> plans;
        for (const auto& row : c.query("SELECT " + join(k_goal_plan_columns) + " FROM goal_plans ORDER BY goal_id, account_id")) {
            auto record = row_dict(row, k_goal_plan_columns);
            plans[pystr(record.get("goal_id"), "goal plan")].push_back(std::move(record));
        }
        for (const auto& row : c.query("SELECT " + join(k_goal_columns) + " FROM goals ORDER BY goal_id")) {
            auto record = row_dict(row, k_goal_columns);
            record.set("plans", Json::array(plans[pystr(record.get("goal_id"), "goal")]));
            records.goals.push_back(std::move(record));
        }
        for (const auto& row : c.query("SELECT " + join(k_override_columns) + " FROM user_overrides ORDER BY override_id")) {
            auto record = row_dict(row, k_override_columns);
            const auto id = pystr(record.get("override_id"), "override");
            record.set("old_projection", archive_json_document(record.get("old_projection_json"),
                                                               "override " + id + ".old_projection_json"));
            record.set("new_user_value", archive_json_document(record.get("new_user_value_json"),
                                                               "override " + id + ".new_user_value_json"));
            record.erase("old_projection_json");
            record.erase("new_user_value_json");
            records.overrides.push_back(std::move(record));
        }
        for (const auto& row : c.query("SELECT " + join(k_receipt_columns) + " FROM calculation_receipts ORDER BY receipt_id")) {
            auto record = row_dict(row, k_receipt_columns);
            const auto id = pystr(record.get("receipt_id"), "receipt");
            (void)required_sha256(record.get("input_hash"), "receipt " + id + ".input_hash");
            const auto refs = archive_json_document(record.get("input_refs_json"), "receipt " + id + ".input_refs_json");
            if (!is_string_list(refs)) fail("receipt " + id + ".input_refs_json must be a string list");
            record.set("input_refs", refs);
            record.set("outputs", archive_json_document(record.get("outputs_json"), "receipt " + id + ".outputs_json", true));
            record.erase("input_refs_json");
            record.erase("outputs_json");
            records.receipts.push_back(std::move(record));
        }
        std::set<std::pair<std::string, std::int64_t>> all_sources;
        for (const auto& [key, value] : sources) all_sources.insert(key);
        if (referenced != all_sources) fail("bundle-finance-1 archive would omit an unreferenced source record");
    } catch (const SqliteError& error) {
        fail(std::string("SQLite export failed: ") + error.what());
    }
    try {
        write_archive(destination, *created, records);
    } catch (const ArchiveError& error) {
        fail(error.what());
    } catch (const JsonError& error) {
        fail(std::string("archive payload is not JSON serializable: ") + error.what());
    }
}

void SqliteFinanceStore::import_archive(const fs::path& path) {
    ArchiveContents archive;
    try {
        archive = read_archive(path);
    } catch (const ArchiveError& error) {
        fail(error.what());
    }
    const auto& accounts = archive.files["accounts.json"];
    const auto& balances = archive.files["balances.jsonl"];
    const auto& transactions = archive.files["transactions.jsonl"];
    const auto& commitments = archive.files["commitments.json"];

    std::map<std::pair<std::string, std::int64_t>, Source> sources;
    const auto collect = [&](const Json& value, const std::string& label) {
        const auto normalized = archive_source(value, label);
        const auto [it, inserted] = sources.emplace(normalized.key(), normalized);
        if (!inserted && !(it->second == normalized)) {
            fail("archive contains conflicting source record ('" + normalized.source_id + "', " +
                 std::to_string(normalized.source_version) + ")");
        }
    };
    std::size_t index = 0;
    for (const auto* list : {&accounts, &balances, &transactions}) {
        for (const auto& record : *list) collect(record.get("source"), "archive record " + std::to_string(index++));
    }
    for (std::size_t ci = 0; ci < commitments.size(); ++ci) {
        const auto evidence = commitments[ci].get("evidence", Json::array());
        if (!evidence.is_array() || !std::all_of(evidence.as_array().begin(), evidence.as_array().end(),
                                                 [](const Json& v) { return v.is_object(); })) {
            fail("archive commitment " + std::to_string(ci) + ".evidence must be a list of objects");
        }
        for (std::size_t ei = 0; ei < evidence.as_array().size(); ++ei) {
            collect(evidence.as_array()[ei].get("source"),
                    "archive commitment " + std::to_string(ci) + ".evidence[" + std::to_string(ei) + "]");
        }
    }
    std::map<std::pair<std::string, std::string>, std::pair<std::int64_t, std::int64_t>> groups;  // max revision, current
    for (std::size_t ti = 0; ti < transactions.size(); ++ti) {
        const auto& record = transactions[ti];
        const auto key = std::make_pair(pystr(record.get("account_id", Json::string("")), "archive transaction"),
                                        pystr(record.get("source_transaction_id", Json::string("")), "archive transaction"));
        const auto revision = record.get("revision");
        const auto current = record.get("current_revision");
        if (!is_int(revision) || revision.as_int() < 1) fail("archive transaction " + std::to_string(ti) + " has an invalid revision");
        if (!is_int(current) || current.as_int() < 1) fail("archive transaction " + std::to_string(ti) + " has an invalid current revision");
        auto [it, inserted] = groups.emplace(key, std::make_pair(revision.as_int(), current.as_int()));
        if (!inserted) {
            if (it->second.second != current.as_int()) fail("archive transaction group has conflicting current revisions");
            it->second.first = std::max(it->second.first, revision.as_int());
        }
    }
    for (const auto& [key, value] : groups) {
        if (value.first != value.second) fail("archive transaction group has an inconsistent current revision");
    }

    auto c = connection_of(db_, &hook_);
    try {
        WriteTransaction transaction(c);
        Adapter adapter(c);
        for (const auto& [key, source] : sources) adapter.ensure_source(source);
        index = 0;
        for (const auto& record : accounts) {
            adapter.restore_account(record, archive_source(record.get("source"), "archive account " + std::to_string(index++)));
        }
        index = 0;
        for (const auto& record : balances) {
            adapter.restore_balance(record, archive_source(record.get("source"), "archive balance " + std::to_string(index++)));
        }
        std::vector<Json> ordered = transactions;
        std::stable_sort(ordered.begin(), ordered.end(), [](const Json& a, const Json& b) {
            return std::make_tuple(*python_str(a.get("account_id", Json::string(""))),
                                   *python_str(a.get("source_transaction_id", Json::string(""))),
                                   a.get("revision", Json::integer(0)).as_int()) <
                   std::make_tuple(*python_str(b.get("account_id", Json::string(""))),
                                   *python_str(b.get("source_transaction_id", Json::string(""))),
                                   b.get("revision", Json::integer(0)).as_int());
        });
        index = 0;
        for (const auto& record : ordered) {
            const auto source = archive_source(record.get("source"), "archive transaction " + std::to_string(index++));
            Json item = record;
            const auto* amount_minor = record.find("amount_minor");
            if (amount_minor == nullptr) fail("archive transaction is missing amount_minor");
            item.set("amount", Json::object({{"minor_units", *amount_minor}, {"currency", record.get("currency")}}));
            item.erase("amount_minor");
            item.set("user_tags", record.get("user_tags", Json::array()));
            adapter.append_transaction(item, source);
        }
        for (const auto& record : commitments) adapter.restore_commitment(record);
        for (const auto& record : archive.files["budgets.json"]) adapter.restore_budget(record);
        for (const auto& record : archive.files["goals.json"]) adapter.restore_goal(record);
        for (const auto& record : archive.files["overrides.jsonl"]) adapter.restore_override(record);
        for (const auto& record : archive.files["receipts.jsonl"]) adapter.restore_receipt(record);
        transaction.commit();
    } catch (const SqliteError& error) {
        fail(std::string("SQLite archive restore failed: ") + error.what());
    }
}

std::vector<Json> SqliteFinanceStore::accounts() const {
    auto c = connection_of(db_, nullptr);
    std::vector<Json> out;
    try {
        for (const auto& row : c.query("SELECT " + join(k_account_columns) + " FROM accounts ORDER BY account_id")) {
            out.push_back(row_dict(row, k_account_columns));
        }
    } catch (const SqliteError& error) {
        fail(std::string("account query failed: ") + error.what());
    }
    return out;
}

std::vector<EventRecord> SqliteFinanceStore::history(const HistoryFilter& filter) const {
    auto c = connection_of(db_, nullptr);
    std::string sql = "SELECT rowid, event_id, event_type, object_id, occurred_at, content_hash FROM event_log";
    std::vector<Json> params;
    std::string where;
    if (filter.object_id) {
        where += "object_id = ?";
        params.push_back(Json::string(*filter.object_id));
    }
    if (filter.event_type) {
        where += std::string(where.empty() ? "" : " AND ") + "event_type = ?";
        params.push_back(Json::string(*filter.event_type));
    }
    if (!where.empty()) sql += " WHERE " + where;
    sql += " ORDER BY rowid";
    if (filter.limit) {
        sql += " LIMIT ?";
        params.push_back(Json::integer(*filter.limit));
    }
    std::vector<EventRecord> events;
    try {
        for (const auto& row : c.query(sql, params)) {
            events.push_back(EventRecord{row[0].as_int(), *python_str(row[1]), *python_str(row[2]), *python_str(row[3]),
                                         *python_str(row[4]), *python_str(row[5])});
        }
    } catch (const SqliteError& error) {
        fail(std::string("history query failed: ") + error.what());
    }
    return events;
}

}  // namespace bundle::persistence
