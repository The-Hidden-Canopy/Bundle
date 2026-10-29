#pragma once

// Minimal RAII wrapper over the SQLite C API used by the native adapter.
// Values cross the boundary as Json so that comparisons follow the Python
// bridge's semantics (sqlite3 module types: int, float, str, None).

#include "bundle/persistence/json.hpp"

#include <sqlite3.h>

#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace bundle::persistence::detail {

class SqliteError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

using Row = std::vector<Json>;

struct Connection final {
    sqlite3* db = nullptr;
    std::function<void(const std::string&)>* hook = nullptr;
    bool in_write_transaction = false;

    [[noreturn]] void raise(const std::string& context) const {
        throw SqliteError(context + ": " + sqlite3_errmsg(db));
    }

    void exec(const std::string& sql) {
        if (in_write_transaction && hook != nullptr && *hook) (*hook)(sql);
        char* message = nullptr;
        if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &message) != SQLITE_OK) {
            std::string text = message != nullptr ? message : "unknown error";
            sqlite3_free(message);
            throw SqliteError(text);
        }
    }

    std::vector<Row> query(const std::string& sql, const std::vector<Json>& params = {}) {
        if (in_write_transaction && hook != nullptr && *hook) (*hook)(sql);
        sqlite3_stmt* statement = nullptr;
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &statement, nullptr) != SQLITE_OK) {
            raise("prepare failed");
        }
        struct Finalizer {
            sqlite3_stmt* s;
            ~Finalizer() { sqlite3_finalize(s); }
        } finalizer{statement};
        for (std::size_t i = 0; i < params.size(); ++i) bind(statement, static_cast<int>(i + 1), params[i]);
        std::vector<Row> rows;
        for (;;) {
            const int rc = sqlite3_step(statement);
            if (rc == SQLITE_DONE) break;
            if (rc != SQLITE_ROW) raise("statement failed");
            Row row;
            const int count = sqlite3_column_count(statement);
            for (int c = 0; c < count; ++c) row.push_back(column(statement, c));
            rows.push_back(std::move(row));
        }
        return rows;
    }

    std::optional<Row> one(const std::string& sql, const std::vector<Json>& params = {}) {
        auto rows = query(sql, params);
        if (rows.empty()) return std::nullopt;
        return std::move(rows.front());
    }

    void run(const std::string& sql, const std::vector<Json>& params = {}) { (void)query(sql, params); }

private:
    void bind(sqlite3_stmt* statement, const int index, const Json& value) {
        int rc = SQLITE_OK;
        switch (value.kind()) {
            case Json::Kind::null: rc = sqlite3_bind_null(statement, index); break;
            case Json::Kind::boolean:  // Python binds bool as int
            case Json::Kind::integer: rc = sqlite3_bind_int64(statement, index, value.as_int()); break;
            case Json::Kind::real: rc = sqlite3_bind_double(statement, index, value.as_real()); break;
            case Json::Kind::string: {
                const auto& text = value.as_string();
                rc = sqlite3_bind_text(statement, index, text.data(), static_cast<int>(text.size()),
                                       SQLITE_TRANSIENT);
                break;
            }
            default:
                throw SqliteError("Error binding parameter " + std::to_string(index) +
                                  ": type is not supported");
        }
        if (rc != SQLITE_OK) raise("bind failed");
    }

    static Json column(sqlite3_stmt* statement, const int index) {
        switch (sqlite3_column_type(statement, index)) {
            case SQLITE_NULL: return Json{};
            case SQLITE_INTEGER: return Json::integer(sqlite3_column_int64(statement, index));
            case SQLITE_FLOAT: return Json::real(sqlite3_column_double(statement, index));
            case SQLITE_TEXT: {
                const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(statement, index));
                return Json::string(std::string(text, static_cast<std::size_t>(sqlite3_column_bytes(statement, index))));
            }
            default: throw SqliteError("BLOB values are not part of the finance schema");
        }
    }
};

// BEGIN IMMEDIATE ... COMMIT, rolled back on any exception.
class WriteTransaction final {
public:
    explicit WriteTransaction(Connection& connection) : connection_(connection) {
        connection_.exec("BEGIN IMMEDIATE");
        connection_.in_write_transaction = true;
    }
    ~WriteTransaction() {
        if (!done_) {
            connection_.in_write_transaction = false;
            sqlite3_exec(connection_.db, "ROLLBACK", nullptr, nullptr, nullptr);
        }
    }
    void commit() {
        connection_.exec("COMMIT");
        connection_.in_write_transaction = false;
        done_ = true;
    }
    WriteTransaction(const WriteTransaction&) = delete;
    WriteTransaction& operator=(const WriteTransaction&) = delete;

private:
    Connection& connection_;
    bool done_ = false;
};

}  // namespace bundle::persistence::detail
