// `bundle` -- native command-line path over the SQLite persistence adapter.
//
// Every command that writes runs in a single SQLite transaction; a rejected
// command leaves the database exactly as it was. Exit codes:
//   0 success, 1 rejected (validation, conflict, tamper), 2 usage error,
//   3 unexpected internal or I/O failure.

#include "bundle/persistence/archive.hpp"
#include "bundle/persistence/csv_import.hpp"
#include "bundle/persistence/sqlite_store.hpp"
#include "bundle/persistence/text.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {

using bundle::persistence::Json;

constexpr int k_ok = 0, k_rejected = 1, k_usage = 2, k_internal = 3;

const char* k_help = R"(bundle -- native BUNDLE finance store (local SQLite, no network)

Usage:
  bundle <command> [options]

Commands:
  init           --db FILE
                 Create or open a database and apply the schema migration.
  import-csv     --db FILE --source-id ID --imported-at TIME [--source-version N] CSV
                 Admit a canonical BUNDLE CSV statement (see docs/file-formats.md).
  import-batch   --db FILE BATCH.json
                 Admit a bundle-connector-1 batch document (e.g. from bundle-csv-import).
  csv-to-batch   --source-id ID --imported-at TIME [--source-version N] CSV
                 Print the bundle-connector-1 batch a CSV would produce. Read-only.
  accounts       --db FILE
                 List accounts with their projected balances.
  history        --db FILE [--object ID] [--type EVENT_TYPE] [--limit N] [--json]
                 Show the append-only event log in the order events were recorded.
  calculate      --db FILE --as-of TIME --horizon-end TIME [--mode confirmed|expected|stress]
                 [--floor-minor N]
                 Compute safe-to-spend from persisted accounts, commitments and goals
                 and record the result as a calculation receipt.
  export         --db FILE --created-at TIME DIR
                 Write a bundle-finance-1 archive to DIR (DIR must not exist).
  restore        --db FILE DIR
                 Restore a bundle-finance-1 archive. Idempotent; all-or-nothing.
  verify-archive DIR
                 Check an archive's manifest, sizes, hashes and JSON. Read-only.
  help | --help  Show this text.      version | --version  Show the version.

TIME is RFC 3339 with an explicit offset, e.g. 2026-01-03T00:00:00Z.
Amounts are integer minor units (cents for USD). Nothing here contacts a bank,
a network service or a model, and the database is not encrypted at rest.

Exit codes: 0 ok, 1 rejected (nothing was written), 2 usage error, 3 internal error.
)";

struct Args final {
    std::string command;
    std::map<std::string, std::string> options;
    std::vector<std::string> flags;
    std::vector<std::string> positional;

    [[nodiscard]] std::optional<std::string> get(const std::string& name) const {
        const auto it = options.find(name);
        return it == options.end() ? std::nullopt : std::optional(it->second);
    }
    [[nodiscard]] bool flag(const std::string& name) const {
        return std::find(flags.begin(), flags.end(), name) != flags.end();
    }
};

class UsageError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

Args parse(int argc, char** argv) {
    Args args;
    if (argc < 2) throw UsageError("missing command");
    args.command = argv[1];
    static const std::vector<std::string> boolean_flags = {"--json"};
    for (int i = 2; i < argc; ++i) {
        const std::string token = argv[i];
        if (token.rfind("--", 0) == 0) {
            if (std::find(boolean_flags.begin(), boolean_flags.end(), token) != boolean_flags.end()) {
                args.flags.push_back(token);
            } else {
                if (i + 1 >= argc) throw UsageError("option " + token + " needs a value");
                args.options[token] = argv[++i];
            }
        } else {
            args.positional.push_back(token);
        }
    }
    return args;
}

std::string require(const Args& args, const std::string& name) {
    const auto value = args.get(name);
    if (!value || value->empty()) throw UsageError(args.command + " requires " + name);
    return *value;
}

void allow_only(const Args& args, std::initializer_list<const char*> names, const std::size_t positional) {
    for (const auto& [name, value] : args.options) {
        if (std::find_if(names.begin(), names.end(), [&](const char* n) { return name == n; }) == names.end()) {
            throw UsageError(args.command + " does not accept " + name);
        }
    }
    if (args.positional.size() != positional) {
        throw UsageError(args.command + " expects " + std::to_string(positional) + " positional argument(s)");
    }
}

std::int64_t integer_option(const Args& args, const std::string& name, const std::int64_t fallback) {
    const auto value = args.get(name);
    if (!value) return fallback;
    try {
        std::size_t used = 0;
        const auto parsed = std::stoll(*value, &used);
        if (used != value->size()) throw std::invalid_argument("trailing");
        return parsed;
    } catch (const std::exception&) {
        throw UsageError(name + " must be an integer");
    }
}

std::string read_file(const std::string& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw bundle::persistence::StoreError("cannot read " + path);
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

std::string time_option(const Args& args, const std::string& name) {
    const auto raw = require(args, name);
    const auto parsed = bundle::persistence::parse_timestamp(raw);
    if (!parsed.value) {
        throw UsageError(name + " must be RFC 3339 with an explicit offset (e.g. 2026-01-03T00:00:00Z), got '" + raw + "'");
    }
    return parsed.value->text;
}

Json csv_batch(const Args& args) {
    bundle::persistence::CsvImportOptions options{require(args, "--source-id"), time_option(args, "--imported-at"),
                                                  integer_option(args, "--source-version", 1)};
    return bundle::persistence::import_csv(read_file(args.positional.at(0)), options);
}

void print_table(const std::vector<std::vector<std::string>>& rows) {
    std::vector<std::size_t> widths;
    for (const auto& row : rows) {
        widths.resize(std::max(widths.size(), row.size()));
        for (std::size_t i = 0; i < row.size(); ++i) widths[i] = std::max(widths[i], row[i].size());
    }
    for (const auto& row : rows) {
        std::string line;
        for (std::size_t i = 0; i < row.size(); ++i) {
            line += row[i];
            if (i + 1 < row.size()) line += std::string(widths[i] - row[i].size() + 2, ' ');
        }
        std::cout << line << "\n";
    }
}

int run(const Args& args) {
    using namespace bundle::persistence;
    const auto& cmd = args.command;
    if (cmd == "help" || cmd == "--help" || cmd == "-h") {
        std::cout << k_help;
        return k_ok;
    }
    if (cmd == "version" || cmd == "--version") {
        std::cout << "bundle " << BUNDLE_VERSION << " (schema migration 1, archive bundle-finance-1, SQLite "
                  << BUNDLE_SQLITE_PROVENANCE << ")\n";
        return k_ok;
    }
    if (cmd == "csv-to-batch") {
        allow_only(args, {"--source-id", "--imported-at", "--source-version"}, 1);
        std::cout << dump_json(csv_batch(args)) << "\n";
        return k_ok;
    }
    if (cmd == "verify-archive") {
        allow_only(args, {}, 1);
        const auto contents = read_archive(args.positional[0]);
        std::size_t total = 0;
        for (const auto& [name, records] : contents.files) total += records.size();
        std::cout << "archive ok: bundle-finance-1, created_at " << contents.manifest.get("created_at").as_string()
                  << ", " << total << " records, all file sizes and SHA-256 digests match\n";
        return k_ok;
    }

    static const std::map<std::string, std::pair<std::vector<const char*>, std::size_t>> shapes = {
        {"init", {{"--db"}, 0}},
        {"import-csv", {{"--db", "--source-id", "--imported-at", "--source-version"}, 1}},
        {"import-batch", {{"--db"}, 1}},
        {"accounts", {{"--db"}, 0}},
        {"history", {{"--db", "--object", "--type", "--limit"}, 0}},
        {"calculate", {{"--db", "--as-of", "--horizon-end", "--mode", "--floor-minor"}, 0}},
        {"export", {{"--db", "--created-at"}, 1}},
        {"restore", {{"--db"}, 1}},
    };
    const auto shape = shapes.find(cmd);
    if (shape == shapes.end()) throw UsageError("unknown command '" + cmd + "'");
    for (const auto& [name, value] : args.options) {
        const auto& allowed = shape->second.first;
        if (std::find_if(allowed.begin(), allowed.end(), [&](const char* n) { return name == n; }) == allowed.end()) {
            throw UsageError(cmd + " does not accept " + name);
        }
    }
    if (args.positional.size() != shape->second.second) {
        throw UsageError(cmd + " expects " + std::to_string(shape->second.second) + " positional argument(s)");
    }

    // Validate every argument before touching (or creating) a database, so a
    // usage error never leaves a new database file behind.
    std::optional<SafeToSpendRequest> calculation;
    if (cmd == "calculate") {
        const auto mode = args.get("--mode").value_or("expected");
        if (mode != "confirmed" && mode != "expected" && mode != "stress") {
            throw UsageError("--mode must be confirmed, expected or stress");
        }
        const auto floor = integer_option(args, "--floor-minor", 0);
        if (floor < 0) throw UsageError("--floor-minor must not be negative");
        calculation = SafeToSpendRequest{time_option(args, "--as-of"), time_option(args, "--horizon-end"), mode, floor};
    }
    std::optional<std::string> created_at;
    if (cmd == "export") created_at = time_option(args, "--created-at");
    std::optional<std::int64_t> limit;
    if (cmd == "history" && args.get("--limit")) {
        limit = integer_option(args, "--limit", 0);
        if (*limit < 0) throw UsageError("--limit must not be negative");
    }
    std::optional<Json> batch;
    if (cmd == "import-csv") batch = csv_batch(args);
    if (cmd == "import-batch") {
        try {
            batch = parse_json(read_file(args.positional[0]));
        } catch (const JsonError& error) {
            throw StoreError(std::string("batch file is not valid JSON: ") + error.what());
        }
    }

    SqliteFinanceStore store(require(args, "--db"));
    if (cmd == "init") {
        std::cout << "database ready: " << require(args, "--db") << " (schema migration 1)\n";
    } else if (batch) {
        const auto before = store.history().size();
        const auto batch_id = store.append_batch(*batch);
        const auto after = store.history().size();
        std::cout << "admitted batch " << batch_id << " (" << (after - before) << " new events"
                  << (after == before ? "; identical batch already admitted, nothing changed" : "") << ")\n";
    } else if (cmd == "accounts") {
        std::vector<std::vector<std::string>> rows = {
            {"ACCOUNT", "TYPE", "CURRENCY", "CURRENT", "AVAILABLE", "FRESHNESS", "OBSERVED_AT", "SOURCE"}};
        const auto accounts = store.accounts();
        for (const auto& a : accounts) {
            const auto text = [&](const char* key) { return python_str(a.get(key)).value_or(""); };
            rows.push_back({text("account_id"), text("account_type"), text("currency"), text("current_minor"),
                            text("available_minor"), text("balance_freshness"), text("balance_observed_at"),
                            text("source_id") + " v" + text("source_version")});
        }
        print_table(rows);
        std::cout << accounts.size() << " account(s)\n";
    } else if (cmd == "history") {
        HistoryFilter filter;
        filter.object_id = args.get("--object");
        filter.event_type = args.get("--type");
        filter.limit = limit;
        const auto events = store.history(filter);
        if (args.flag("--json")) {
            for (const auto& e : events) {
                std::cout << dump_json(Json::object({{"sequence", Json::integer(e.sequence)},
                                                     {"event_id", Json::string(e.event_id)},
                                                     {"event_type", Json::string(e.event_type)},
                                                     {"object_id", Json::string(e.object_id)},
                                                     {"occurred_at", Json::string(e.occurred_at)},
                                                     {"content_hash", Json::string(e.content_hash)}}))
                          << "\n";
            }
        } else {
            std::vector<std::vector<std::string>> rows = {{"SEQ", "OCCURRED_AT", "EVENT_TYPE", "OBJECT", "EVENT_ID"}};
            for (const auto& e : events) {
                rows.push_back({std::to_string(e.sequence), e.occurred_at, e.event_type, e.object_id, e.event_id});
            }
            print_table(rows);
            std::cout << events.size() << " event(s)\n";
        }
    } else if (cmd == "calculate") {
        const auto receipt = store.calculate_safe_to_spend(*calculation);
        const auto outputs = receipt.record.get("outputs");
        std::cout << "safe_to_spend_minor " << outputs.get("safe_to_spend_minor").as_int() << " "
                  << outputs.get("currency").as_string() << "\n"
                  << "receipt " << receipt.receipt_id << (receipt.newly_recorded ? " (recorded)" : " (already recorded; identical inputs)")
                  << "\n"
                  << dump_json(receipt.record) << "\n";
    } else if (cmd == "export") {
        store.export_archive(args.positional[0], *created_at);
        std::cout << "exported bundle-finance-1 archive to " << args.positional[0] << "\n";
    } else if (cmd == "restore") {
        const auto before = store.history().size();
        store.import_archive(args.positional[0]);
        std::cout << "restored " << args.positional[0] << " (" << (store.history().size() - before)
                  << " new events)\n";
    }
    return k_ok;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        return run(parse(argc, argv));
    } catch (const UsageError& error) {
        std::cerr << "bundle: " << error.what() << "\nRun 'bundle help' for usage.\n";
        return k_usage;
    } catch (const bundle::persistence::StoreError& error) {
        std::cerr << "bundle: rejected: " << error.what() << "\n";
        return k_rejected;
    } catch (const bundle::persistence::ArchiveError& error) {
        std::cerr << "bundle: rejected: " << error.what() << "\n";
        return k_rejected;
    } catch (const bundle::persistence::CsvImportError& error) {
        std::cerr << "bundle: rejected: " << error.what() << "\n";
        return k_rejected;
    } catch (const std::exception& error) {
        std::cerr << "bundle: internal error: " << error.what() << "\n";
        return k_internal;
    }
}
