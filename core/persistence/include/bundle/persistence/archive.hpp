#pragma once

// bundle-finance-1 archive files, byte-compatible with
// connectors/bundle_connectors/archive.py.

#include "bundle/persistence/json.hpp"

#include <array>
#include <filesystem>
#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace bundle::persistence {

class ArchiveError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

inline constexpr std::array<const char*, 8> k_archive_files = {
    "accounts.json",   "balances.jsonl", "transactions.jsonl", "commitments.json",
    "budgets.json",    "goals.json",     "overrides.jsonl",    "receipts.jsonl",
};

struct ArchiveRecords final {
    std::vector<Json> accounts;
    std::vector<Json> balances;
    std::vector<Json> transactions;
    std::vector<Json> commitments;
    std::vector<Json> budgets;
    std::vector<Json> goals;
    std::vector<Json> overrides;
    std::vector<Json> receipts;
};

// write_export(). `created_at` must already be normalized UTC text. The
// destination must not exist; files are written to a temporary sibling
// directory and renamed into place so an interrupted export never leaves a
// partial archive at the destination path.
void write_archive(const std::filesystem::path& destination, const std::string& created_at,
                   const ArchiveRecords& records);

struct ArchiveContents final {
    Json manifest;
    std::map<std::string, std::vector<Json>> files;  // file name -> records
};

// read_export(): verifies the manifest shape, every file's byte count and
// SHA-256, UTF-8, JSON and JSONL structure before returning anything.
[[nodiscard]] ArchiveContents read_archive(const std::filesystem::path& source);

// Test seam: called after each archive file is written to the staging
// directory (used to simulate an interrupted export). Not used in production.
void set_archive_write_hook(std::function<void(const std::string& file)> hook);

// The exact bytes write_archive() would produce for one file (exposed for tests).
[[nodiscard]] std::string archive_json_bytes(const Json& value);
[[nodiscard]] std::string archive_jsonl_bytes(std::vector<Json> records);

}  // namespace bundle::persistence
