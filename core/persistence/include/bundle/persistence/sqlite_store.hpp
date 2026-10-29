#pragma once

// Native SQLite persistence adapter (risk register R-003).
//
// Behavioral port of connectors/bundle_connectors/sqlite_store.py over the same
// migration (core/persistence/migrations/001_finance.sql). Every mutating
// operation runs inside one BEGIN IMMEDIATE transaction and either commits
// completely or raises StoreError after rolling back. The adapter never opens a
// network connection and is not an encryption boundary (see risk R-005).

#include "bundle/persistence/json.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

struct sqlite3;

namespace bundle::persistence {

class StoreError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct EventRecord final {
    std::int64_t sequence;  // event_log rowid: the order events were appended
    std::string event_id;
    std::string event_type;
    std::string object_id;
    std::string occurred_at;
    std::string content_hash;
};

struct HistoryFilter final {
    std::optional<std::string> object_id;
    std::optional<std::string> event_type;
    std::optional<std::int64_t> limit;
};

// A hypothetical one-off outflow evaluated with bundle::evaluate_scenario.
struct WhatIfSpend final {
    std::int64_t amount_minor = 0;
    std::string date;  // RFC 3339
    std::string account_id;
    std::optional<std::string> description;
};

struct SafeToSpendRequest final {
    std::string as_of;        // RFC 3339; becomes the forecast start and receipt time
    std::string horizon_end;  // RFC 3339
    std::string confidence_mode = "expected";  // confirmed | expected | stress
    std::int64_t protected_floor_minor = 0;
    // false: compute only (nothing is written). A what-if is never recorded.
    bool record = true;
    std::optional<WhatIfSpend> what_if;
};

struct SafeToSpendReceipt final {
    std::string receipt_id;
    bool newly_recorded;
    Json record;    // the receipt in bundle-finance-1 archive form
    Json forecast;  // projected points (not part of the receipt or archive)
    Json what_if;   // null, or the scenario comparison
    bool stored = false;  // a receipt with this ID exists in the database
};

struct SnapshotOptions final {
    std::int64_t transfer_window_hours = 72;
    std::int64_t refund_window_days = 90;
};

class SqliteFinanceStore final {
public:
    // Opens (or creates) a local database file, applies the migration
    // idempotently and validates persisted digests, failing closed on any
    // malformed legacy hash exactly like the Python bridge.
    explicit SqliteFinanceStore(const std::filesystem::path& database,
                                std::optional<std::string> migration_sql = std::nullopt);
    ~SqliteFinanceStore();
    SqliteFinanceStore(const SqliteFinanceStore&) = delete;
    SqliteFinanceStore& operator=(const SqliteFinanceStore&) = delete;

    // Admits a bundle-connector-1 batch atomically; returns the deterministic
    // batch ID. Re-admitting an identical batch is a no-op that returns the
    // same ID; a changed batch reusing a source identity is rejected.
    std::string append_batch(const Json& batch_document);

    // Runs the complete admission for a batch inside a transaction that is
    // always rolled back: same validation and conflict checks, nothing written.
    struct BatchPreview final {
        std::string batch_id;
        bool already_admitted;   // an identical batch was admitted before
        std::int64_t new_events; // events the real admission would append
    };
    [[nodiscard]] BatchPreview preview_batch(const Json& batch_document);

    // Exports the represented slice as bundle-finance-1; refuses non-empty
    // tables the format cannot represent.
    void export_archive(const std::filesystem::path& destination, const std::string& created_at);

    // Restores an archive atomically and idempotently.
    void import_archive(const std::filesystem::path& source);

    [[nodiscard]] std::vector<EventRecord> history(const HistoryFilter& filter = {}) const;

    // Account projections ordered by account_id (column names as in the schema).
    [[nodiscard]] std::vector<Json> accounts() const;

    // Deterministic safe-to-spend calculation over persisted accounts,
    // commitments and goals, recorded as an append-only calculation receipt.
    SafeToSpendReceipt calculate_safe_to_spend(const SafeToSpendRequest& request);

    // Read-only view for user interfaces: accounts, current transaction
    // revisions classified by the core's transfer/refund recognizers, planning
    // records, receipts, import batches, sources and export blockers.
    [[nodiscard]] Json snapshot(const SnapshotOptions& options = {}) const;

    [[nodiscard]] sqlite3* handle() const noexcept { return db_; }

    // Test seam: called before each statement executed inside a write
    // transaction (used to simulate a crash mid-transaction).
    void set_statement_hook(std::function<void(const std::string&)> hook) { hook_ = std::move(hook); }

    [[nodiscard]] static const std::string& embedded_migration();

private:
    friend class Transaction;
    sqlite3* db_ = nullptr;
    std::function<void(const std::string&)> hook_;
    bool preview_only_ = false;
    std::int64_t* preview_counter_ = nullptr;
};

// The connector-batch schema checks applied before admission
// (ConnectorBatch.__post_init__ plus the bundle-connector-1 schema shape).
void validate_connector_batch(const Json& batch_document);

}  // namespace bundle::persistence
