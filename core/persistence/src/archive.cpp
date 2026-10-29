#include "bundle/persistence/archive.hpp"

#include "bundle/persistence/text.hpp"

#include <algorithm>
#include <fstream>
#include <random>
#include <set>
#include <sstream>

namespace bundle::persistence {
namespace fs = std::filesystem;
namespace {

std::function<void(const std::string&)>& write_hook() {
    static std::function<void(const std::string&)> hook;
    return hook;
}

constexpr std::array<const char*, 9> k_secret_parts = {
    "password", "secret", "credential", "access_token", "auth_token",
    "refresh_token", "api_key", "api-key", "private_key",
};

std::string ascii_lower(std::string value) {
    for (auto& c : value) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return value;
}

void reject_secrets(const Json& value, const std::string& path = "record") {
    if (value.is_object()) {
        for (const auto& [key, child] : value.as_object()) {
            const auto lowered = ascii_lower(key);
            for (const auto* part : k_secret_parts) {
                if (lowered.find(part) != std::string::npos) {
                    throw ArchiveError("export contains forbidden secret field at " + path + "." + key);
                }
            }
            reject_secrets(child, path + "." + key);
        }
    } else if (value.is_array()) {
        std::size_t index = 0;
        for (const auto& child : value.as_array()) {
            reject_secrets(child, path + "[" + std::to_string(index++) + "]");
        }
    }
}

std::string python_text(const Json& value) {
    const auto text = python_str(value);
    if (!text.has_value()) {
        throw ArchiveError("archive record identifier must be a scalar value");
    }
    return *text;
}

std::string read_file(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw ArchiveError("could not read export file: " + path.filename().string());
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

void write_file(const fs::path& path, const std::string& bytes) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    stream.flush();
    if (!stream) {
        throw ArchiveError("could not write export file: " + path.filename().string());
    }
}

std::vector<Json> sorted_by(std::vector<Json> records, const char* field) {
    std::stable_sort(records.begin(), records.end(), [field](const Json& a, const Json& b) {
        return python_text(a.get(field, Json::string(""))) < python_text(b.get(field, Json::string("")));
    });
    return records;
}

}  // namespace

void set_archive_write_hook(std::function<void(const std::string& file)> hook) { write_hook() = std::move(hook); }

std::string archive_json_bytes(const Json& value) {
    reject_secrets(value);
    return canonical_json(value) + "\n";
}

std::string archive_jsonl_bytes(std::vector<Json> records) {
    static constexpr std::array<const char*, 8> fields = {
        "id", "transaction_id", "observation_id", "commitment_id",
        "budget_id", "goal_id", "override_id", "receipt_id",
    };
    using Key = std::pair<std::string, std::string>;
    std::vector<std::pair<Key, Json>> keyed;
    keyed.reserve(records.size());
    for (auto& record : records) {
        if (!record.is_object()) {
            throw ArchiveError("JSONL records must contain only objects");
        }
        Key key{"record", ""};
        bool found = false;
        for (const auto* field : fields) {
            const auto* identifier = record.find(field);
            if (identifier != nullptr && !identifier->is_null()) {
                key = {field, python_text(*identifier)};
                found = true;
                break;
            }
        }
        if (!found) key.second = canonical_json(record);
        keyed.emplace_back(std::move(key), std::move(record));
    }
    std::stable_sort(keyed.begin(), keyed.end(),
                     [](const auto& a, const auto& b) { return a.first < b.first; });
    std::string out;
    for (const auto& [key, record] : keyed) {
        reject_secrets(record);
        out += canonical_json(record) + "\n";
    }
    return out;
}

void write_archive(const fs::path& destination, const std::string& created_at,
                   const ArchiveRecords& records) {
    if (!parse_timestamp(created_at).value.has_value()) {
        throw ArchiveError("export created_at must be a timezone-aware timestamp");
    }
    std::error_code ec;
    if (fs::exists(destination, ec)) {
        throw ArchiveError("export destination already exists");
    }
    const auto as_array = [](const std::vector<Json>& values) { return Json::array(values); };
    std::map<std::string, std::string> payloads;
    payloads["accounts.json"] = archive_json_bytes(as_array(sorted_by(records.accounts, "account_id")));
    payloads["balances.jsonl"] = archive_jsonl_bytes(records.balances);
    payloads["transactions.jsonl"] = archive_jsonl_bytes(records.transactions);
    payloads["commitments.json"] = archive_json_bytes(as_array(sorted_by(records.commitments, "commitment_id")));
    payloads["budgets.json"] = archive_json_bytes(as_array(sorted_by(records.budgets, "budget_id")));
    payloads["goals.json"] = archive_json_bytes(as_array(sorted_by(records.goals, "goal_id")));
    payloads["overrides.jsonl"] = archive_jsonl_bytes(records.overrides);
    payloads["receipts.jsonl"] = archive_jsonl_bytes(records.receipts);

    Json::Array files;
    for (const auto* name : k_archive_files) {
        const auto& bytes = payloads.at(name);
        files.push_back(Json::object({
            {"path", Json::string(name)},
            {"bytes", Json::integer(static_cast<std::int64_t>(bytes.size()))},
            {"sha256", Json::string(sha256_hex(bytes))},
        }));
    }
    const auto manifest = archive_json_bytes(Json::object({
        {"schema", Json::string("bundle-finance-1")},
        {"created_at", Json::string(parse_timestamp(created_at).value->text)},
        {"files", Json::array(std::move(files))},
    }));

    const auto parent = destination.has_parent_path() ? destination.parent_path() : fs::path(".");
    fs::create_directories(parent, ec);
    if (ec) {
        throw ArchiveError("could not create export destination");
    }
    std::random_device random;
    const auto staging = parent / (destination.filename().string() + ".partial-" + std::to_string(random()));
    if (!fs::create_directory(staging, ec) || ec) {
        throw ArchiveError("could not create export staging directory");
    }
    try {
        for (const auto* name : k_archive_files) {
            write_file(staging / name, payloads.at(name));
            if (write_hook()) write_hook()(name);
        }
        write_file(staging / "manifest.json", manifest);
        if (write_hook()) write_hook()("manifest.json");
        fs::rename(staging, destination);
    } catch (...) {
        fs::remove_all(staging, ec);
        throw;
    }
}

ArchiveContents read_archive(const fs::path& source) {
    std::error_code ec;
    if (!fs::is_directory(source, ec)) {
        throw ArchiveError("export source must be a directory");
    }
    const auto manifest_path = source / "manifest.json";
    if (!fs::is_regular_file(manifest_path, ec)) {
        throw ArchiveError("manifest.json is required");
    }
    Json manifest;
    try {
        manifest = parse_json(read_file(manifest_path));
    } catch (const JsonError&) {
        throw ArchiveError("manifest.json is not valid UTF-8 JSON");
    }
    const auto keys_are = [](const Json& object, std::set<std::string> expected) {
        if (!object.is_object() || object.as_object().size() != expected.size()) return false;
        for (const auto& [key, value] : object.as_object()) {
            if (expected.erase(key) != 1) return false;
        }
        return expected.empty();
    };
    if (!keys_are(manifest, {"schema", "created_at", "files"}) ||
        manifest.get("schema") != Json::string("bundle-finance-1")) {
        throw ArchiveError("unsupported export schema");
    }
    const auto created_at = manifest.get("created_at");
    if (!created_at.is_string()) {
        throw ArchiveError("manifest created_at must be a timestamp");
    }
    if (!parse_timestamp(created_at.as_string()).value.has_value()) {
        throw ArchiveError("manifest created_at must be a timezone-aware timestamp");
    }
    const auto entries = manifest.get("files");
    const auto bad_list = [] { return ArchiveError("manifest file list does not match bundle-finance-1"); };
    if (!entries.is_array() || entries.as_array().size() != k_archive_files.size()) throw bad_list();
    std::set<std::string> paths;
    for (const auto& entry : entries.as_array()) {
        if (!keys_are(entry, {"path", "bytes", "sha256"})) throw bad_list();
        const auto path = entry.get("path");
        const auto bytes = entry.get("bytes");
        const auto digest = entry.get("sha256");
        if (!path.is_string() || !bytes.is_int() || bytes.as_int() < 0 || !digest.is_string() ||
            digest.as_string().size() != 64 ||
            digest.as_string().find_first_not_of("0123456789abcdef") != std::string::npos) {
            throw bad_list();
        }
        paths.insert(path.as_string());
    }
    if (paths != std::set<std::string>(k_archive_files.begin(), k_archive_files.end())) throw bad_list();

    ArchiveContents output;
    output.manifest = manifest;
    for (const auto& entry : entries.as_array()) {
        const auto name = entry.get("path").as_string();
        // Names are drawn from the fixed list above, so no traversal is possible.
        const auto path = source / name;
        if (!fs::is_regular_file(path, ec)) {
            throw ArchiveError("missing export file: " + name);
        }
        const auto payload = read_file(path);
        if (static_cast<std::int64_t>(payload.size()) != entry.get("bytes").as_int() ||
            sha256_hex(payload) != entry.get("sha256").as_string()) {
            throw ArchiveError("hash or size mismatch for " + name);
        }
        std::vector<Json> parsed;
        try {
            if (!valid_utf8(payload)) throw JsonError("not UTF-8");
            if (name.size() > 6 && name.substr(name.size() - 6) == ".jsonl") {
                // JSONL is newline-delimited. The Python reader uses str.splitlines(),
                // which also splits inside raw U+2028/U+2029/U+0085 text it wrote
                // itself; see docs/native-sqlite-adapter.md (reference finding B).
                std::size_t start = 0;
                while (start < payload.size()) {
                    auto end = payload.find('\n', start);
                    if (end == std::string::npos) end = payload.size();
                    const auto line = std::string_view(payload).substr(start, end - start);
                    if (line.find_first_not_of(" \t\r\f\v") == std::string_view::npos) {
                        throw ArchiveError("export file contains a blank JSONL line: " + name);
                    }
                    parsed.push_back(parse_json(line));
                    start = end + 1;
                }
            } else {
                const auto document = parse_json(payload);
                if (!document.is_array()) {
                    throw ArchiveError("export file must contain a list of objects: " + name);
                }
                parsed = document.as_array();
            }
        } catch (const JsonError&) {
            throw ArchiveError("export file is not valid JSON: " + name);
        }
        for (const auto& record : parsed) {
            if (!record.is_object()) {
                throw ArchiveError("export file must contain a list of objects: " + name);
            }
        }
        output.files[name] = std::move(parsed);
    }
    return output;
}

}  // namespace bundle::persistence
