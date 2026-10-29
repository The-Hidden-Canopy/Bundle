// Interrupted-write, rollback, restart and recovery tests for the native adapter.
//
// For each write operation the test re-runs a child copy of this executable that
// calls std::_Exit at statement N of the write transaction (no destructors, no
// rollback, no flush), for N = 1, 2, ... until the operation completes. After
// every kill the parent "restarts" by reopening the database through the normal
// open path and requires the database to be byte-for-byte unchanged in content
// and to pass PRAGMA integrity_check. The completed run must match a clean run.

#include "bundle/persistence/archive.hpp"
#include "bundle/persistence/csv_import.hpp"
#include "bundle/persistence/sqlite_store.hpp"
#include "bundle_test/check.hpp"

#include <sqlite3.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;
using namespace bundle::persistence;

namespace {

constexpr int k_crashed = 77;
fs::path g_self;

std::string read(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    std::ostringstream out;
    out << stream.rdbuf();
    return out.str();
}

fs::path fixture(const char* name) { return fs::path(BUNDLE_PARITY_FIXTURES) / name; }

// Every row of every table, typed, in rowid order -- read after a normal reopen.
std::string dump(const fs::path& database) {
    SqliteFinanceStore store(database);  // the restart path: migration + digest validation
    sqlite3* db = store.handle();
    std::string out;
    sqlite3_stmt* tables = nullptr;
    sqlite3_prepare_v2(db, "SELECT name FROM sqlite_master WHERE type='table' ORDER BY name", -1, &tables, nullptr);
    while (sqlite3_step(tables) == SQLITE_ROW) {
        const std::string table = reinterpret_cast<const char*>(sqlite3_column_text(tables, 0));
        out += "## " + table + "\n";
        sqlite3_stmt* rows = nullptr;
        const auto sql = table == "schema_migrations" ? "SELECT version FROM schema_migrations"
                                                      : "SELECT * FROM " + table + " ORDER BY rowid";
        sqlite3_prepare_v2(db, sql.c_str(), -1, &rows, nullptr);
        while (sqlite3_step(rows) == SQLITE_ROW) {
            for (int c = 0; c < sqlite3_column_count(rows); ++c) {
                const int type = sqlite3_column_type(rows, c);
                out += std::to_string(type) + ":";
                if (type != SQLITE_NULL) out += reinterpret_cast<const char*>(sqlite3_column_text(rows, c));
                out += "|";
            }
            out += "\n";
        }
        sqlite3_finalize(rows);
    }
    sqlite3_finalize(tables);
    sqlite3_stmt* check = nullptr;
    sqlite3_prepare_v2(db, "PRAGMA integrity_check", -1, &check, nullptr);
    CHECK(sqlite3_step(check) == SQLITE_ROW);
    const std::string verdict = reinterpret_cast<const char*>(sqlite3_column_text(check, 0));
    sqlite3_finalize(check);
    CHECK_EQ(verdict, std::string("ok"));
    return out;
}

int run_child(int argc, char** argv) {
    // --child MODE DB CRASH_AT INPUT
    const std::string mode = argv[2];
    const fs::path database = argv[3];
    const int crash_at = std::atoi(argv[4]);
    const std::string input = argc > 5 ? argv[5] : "";
    int count = 0;
    const auto maybe_crash = [&](const std::string&) {
        if (++count == crash_at) std::_Exit(k_crashed);
    };
    try {
        SqliteFinanceStore store(database);
        store.set_statement_hook(maybe_crash);
        if (mode == "import") {
            store.append_batch(parse_json(read(input)));
        } else if (mode == "restore") {
            store.import_archive(input);
        } else if (mode == "calculate") {
            (void)store.calculate_safe_to_spend({"2026-01-04T12:00:00Z", "2026-02-28T00:00:00Z", "expected", 20000});
        } else if (mode == "export") {
            set_archive_write_hook(maybe_crash);
            store.export_archive(input, "2026-01-06T00:00:00Z");
        }
    } catch (const std::exception& error) {
        std::cerr << "child failed: " << error.what() << "\n";
        return 1;
    }
    return 0;
}

int spawn(const std::string& mode, const fs::path& database, const int crash_at, const fs::path& input) {
    std::string command = "\"" + g_self.string() + "\" --child " + mode + " \"" + database.string() + "\" " +
                          std::to_string(crash_at) + " \"" + input.string() + "\"";
#ifdef _WIN32
    command = "\"" + command + "\"";
    return std::system(command.c_str());
#else
    const int status = std::system(command.c_str());
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
}

void copy_db(const fs::path& from, const fs::path& to) {
    for (const auto* suffix : {"", "-wal", "-shm"}) fs::remove(to.string() + suffix);
    fs::copy_file(from, to);
}

// Kill the operation at every statement; every kill must leave the baseline intact.
void crash_at_every_statement(const std::string& mode, const fs::path& baseline, const fs::path& input,
                              const fs::path& work_dir, const int minimum_crash_points) {
    const auto baseline_dump = dump(baseline);
    const auto reference = work_dir / (mode + "-reference.db");
    copy_db(baseline, reference);
    CHECK_EQ(spawn(mode, reference, 0, input), 0);  // uninterrupted run
    const auto completed_dump = dump(reference);
    CHECK(completed_dump != baseline_dump);

    int crashes = 0;
    for (int n = 1;; ++n) {
        const auto work = work_dir / (mode + "-work.db");
        copy_db(baseline, work);
        const int code = spawn(mode, work, n, input);
        if (code == k_crashed) {
            ++crashes;
            CHECK(dump(work) == baseline_dump);  // nothing partial survived the crash
            CHECK_EQ(spawn(mode, work, 0, input), 0);  // restart and retry succeeds
            CHECK(dump(work) == completed_dump);
            continue;
        }
        CHECK_EQ(code, 0);
        CHECK(dump(work) == completed_dump);
        break;
    }
    std::cout << mode << ": " << crashes << " crash points, every one recovered to the pre-write state\n";
    CHECK(crashes >= minimum_crash_points);
}

void export_interrupted_after_every_file(const fs::path& database, const fs::path& work_dir) {
    const auto reference = work_dir / "export-reference";
    CHECK_EQ(spawn("export", database, 0, reference), 0);
    const auto expected = read_archive(reference);
    for (int n = 1; n <= 9; ++n) {
        const auto destination = work_dir / ("export-" + std::to_string(n));
        CHECK_EQ(spawn("export", database, n, destination), k_crashed);
        CHECK(!fs::exists(destination));  // an interrupted export never appears at the destination
        CHECK_EQ(spawn("export", database, 0, destination), 0);
        for (const auto* name : k_archive_files) CHECK(read(destination / name) == read(reference / name));
        CHECK(read(destination / "manifest.json") == read(reference / "manifest.json"));
    }
    (void)expected;
    std::cout << "export: interrupted after each of 9 files; destination never partially written\n";
}

void in_process_rollback(const fs::path& baseline, const fs::path& work_dir) {
    const auto work = work_dir / "rollback.db";
    copy_db(baseline, work);
    const auto before = dump(work);
    auto batch = parse_json(read(fixture("batch-bank-v1.json")));
    // Conflict in the *last* statement group: earlier rows of the same batch must not persist.
    auto tx = batch.get("transactions");
    auto conflicting = tx.as_array().at(0);
    conflicting.set("transaction_id", Json::string("card-002"));
    conflicting.set("source_transaction_id", Json::string("card-002"));
    auto amount = conflicting.get("amount");
    amount.set("currency", Json::string("EUR"));  // rejected after accounts, balances and card-001 were written
    conflicting.set("amount", amount);
    tx.as_array().push_back(conflicting);
    batch.set("transactions", tx);
    {
        SqliteFinanceStore store(work);
        CHECK_THROWS(store.append_batch(batch), StoreError);
    }
    CHECK(dump(work) == before);
    std::cout << "rollback: rejected late in the transaction, database unchanged\n";
}

}  // namespace

int main(int argc, char** argv) {
    g_self = fs::absolute(argv[0]);
    if (argc > 1 && std::string(argv[1]) == "--child") return run_child(argc, argv);

    const auto work_dir = fs::temp_directory_path() / ("bundle-recovery-" + std::to_string(std::rand()) +
                                                        std::to_string(reinterpret_cast<std::uintptr_t>(&argc) % 100000));
    fs::remove_all(work_dir);
    fs::create_directories(work_dir);

    // Baseline: the CSV statement only.
    const auto csv_only = work_dir / "csv-only.db";
    {
        SqliteFinanceStore store(csv_only);
        store.append_batch(import_csv(read(fixture("statement.csv")), {"csv-2026-01", "2026-01-04T12:00:00Z", 1}));
    }
    crash_at_every_statement("import", csv_only, fixture("batch-bank-v1.json"), work_dir, 10);
    in_process_rollback(csv_only, work_dir);

    // A full database with planning rows, its archive, and a clean target.
    const auto full = work_dir / "full.db";
    copy_db(csv_only, full);
    {
        SqliteFinanceStore store(full);
        store.append_batch(parse_json(read(fixture("batch-bank-v1.json"))));
        CHECK(sqlite3_exec(store.handle(),
                           "INSERT INTO commitments VALUES ('rent','Rent',NULL,"
                           "'{\"kind\":\"fixed\",\"primary\":{\"currency\":\"USD\",\"minor_units\":95000}}','USD',"
                           "'monthly','2026-02-01T00:00:00Z',0,'housing','active','supported',1,"
                           "'2026-01-04T12:00:00Z','2026-01-04T12:00:00Z');"
                           "INSERT INTO commitment_evidence VALUES ('rent','csv-2026-01',1,'chk-002');"
                           "INSERT INTO goals VALUES ('buffer','Buffer',200000,'USD','2026-02-15T00:00:00Z',"
                           "50000,25000,1,1,'active');"
                           "INSERT INTO goal_plans VALUES ('buffer','checking');",
                           nullptr, nullptr, nullptr) == SQLITE_OK);
        store.export_archive(work_dir / "seed-archive", "2026-01-06T00:00:00Z");
    }
    const auto empty = work_dir / "empty.db";
    { SqliteFinanceStore store(empty); }
    crash_at_every_statement("restore", empty, work_dir / "seed-archive", work_dir, 20);

    const auto restored = work_dir / "restored.db";
    copy_db(empty, restored);
    CHECK_EQ(spawn("restore", restored, 0, work_dir / "seed-archive"), 0);
    crash_at_every_statement("calculate", restored, "", work_dir, 3);
    export_interrupted_after_every_file(full, work_dir);

    fs::remove_all(work_dir);
    return 0;
}
