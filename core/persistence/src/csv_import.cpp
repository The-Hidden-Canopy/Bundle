#include "bundle/persistence/csv_import.hpp"

#include "bundle/persistence/text.hpp"

#include <map>
#include <set>
#include <vector>

namespace bundle::persistence {
namespace {

[[noreturn]] void fail(const std::string& message) { throw CsvImportError(message); }

const std::set<std::string> k_required = {"account_id", "account_name", "account_type", "currency",
                                          "transaction_id", "amount_minor", "direction", "state",
                                          "description", "observed_at"};
const std::set<std::string> k_currencies = {"USD", "EUR", "GBP", "JPY", "KWD"};
const std::set<std::string> k_directions = {"debit", "credit"};
const std::set<std::string> k_states = {"pending", "posted", "reversed", "removed", "unknown"};

// Python's csv.reader (excel dialect: ',' delimiter, '"' quote, doublequote,
// strict=False) fed by a text stream opened with newline="".
std::vector<std::vector<std::string>> parse_csv(const std::string_view text) {
    enum class State { start_record, start_field, in_field, in_quoted, quote_in_quoted, eat_crnl };
    std::vector<std::vector<std::string>> records;
    std::vector<std::string> record;
    std::string field;
    State state = State::start_record;
    const auto save_field = [&] {
        record.push_back(field);
        field.clear();
    };
    const auto end_record = [&] {
        records.push_back(record);
        record.clear();
    };
    // Lines split on \n, \r or \r\n with the terminators kept (universal newlines, untranslated).
    std::size_t index = 0;
    while (index < text.size()) {
        std::size_t end = index;
        while (end < text.size() && text[end] != '\n' && text[end] != '\r') ++end;
        if (end < text.size()) {
            end += (text[end] == '\r' && end + 1 < text.size() && text[end + 1] == '\n') ? 2 : 1;
        }
        const auto line = text.substr(index, end - index);
        index = end;
        for (std::size_t i = 0; i <= line.size(); ++i) {
            const bool eol = i == line.size();
            const char c = eol ? '\0' : line[i];
            if (!eol && c == '\0') fail("line contains NUL");
            switch (state) {
                case State::start_record:
                    if (eol) {
                        records.emplace_back();  // empty line: DictReader skips it
                        break;
                    }
                    if (c == '\n' || c == '\r') {
                        state = State::eat_crnl;
                        break;
                    }
                    state = State::start_field;
                    [[fallthrough]];
                case State::start_field:
                    if (eol || c == '\n' || c == '\r') {
                        save_field();
                        end_record();
                        state = eol ? State::start_record : State::eat_crnl;
                    } else if (c == '"') {
                        state = State::in_quoted;
                    } else if (c == ',') {
                        save_field();
                    } else {
                        field.push_back(c);
                        state = State::in_field;
                    }
                    break;
                case State::in_field:
                    if (eol || c == '\n' || c == '\r') {
                        save_field();
                        end_record();
                        state = eol ? State::start_record : State::eat_crnl;
                    } else if (c == ',') {
                        save_field();
                        state = State::start_field;
                    } else {
                        field.push_back(c);
                    }
                    break;
                case State::in_quoted:
                    if (eol) break;  // newline characters inside quotes were already added
                    if (c == '"') state = State::quote_in_quoted;
                    else field.push_back(c);
                    break;
                case State::quote_in_quoted:
                    if (!eol && c == '"') {
                        field.push_back('"');
                        state = State::in_quoted;
                    } else if (!eol && c == ',') {
                        save_field();
                        state = State::start_field;
                    } else if (eol || c == '\n' || c == '\r') {
                        save_field();
                        end_record();
                        state = eol ? State::start_record : State::eat_crnl;
                    } else {
                        field.push_back(c);
                        state = State::in_field;
                    }
                    break;
                case State::eat_crnl:
                    if (eol) state = State::start_record;
                    else if (c != '\n' && c != '\r') fail("new-line character seen in unquoted field");
                    break;
            }
        }
    }
    if (state == State::in_quoted || state == State::quote_in_quoted ||
        (state != State::start_record && state != State::eat_crnl && !field.empty())) {
        save_field();  // strict=False: a dangling quoted field ends the record at EOF
        end_record();
    }
    return records;
}

// str.strip() with no arguments (Unicode whitespace per str.isspace()).
std::string py_strip(const std::string& value) {
    static const std::set<char32_t> spaces = {
        0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x1C, 0x1D, 0x1E, 0x1F, 0x20, 0x85, 0xA0, 0x1680,
        0x2000, 0x2001, 0x2002, 0x2003, 0x2004, 0x2005, 0x2006, 0x2007, 0x2008, 0x2009, 0x200A,
        0x2028, 0x2029, 0x202F, 0x205F, 0x3000};
    std::vector<std::pair<std::size_t, char32_t>> points;  // byte offset, code point
    for (std::size_t i = 0; i < value.size();) {
        const auto b = static_cast<unsigned char>(value[i]);
        const std::size_t len = b < 0x80 ? 1 : (b & 0xE0) == 0xC0 ? 2 : (b & 0xF0) == 0xE0 ? 3 : 4;
        char32_t cp = len == 1 ? b : len == 2 ? (b & 0x1F) : len == 3 ? (b & 0x0F) : (b & 0x07);
        for (std::size_t k = 1; k < len && i + k < value.size(); ++k) cp = (cp << 6) | (value[i + k] & 0x3F);
        points.emplace_back(i, cp);
        i += len;
    }
    std::size_t first = 0, last = points.size();
    while (first < last && spaces.contains(points[first].second)) ++first;
    while (last > first && spaces.contains(points[last - 1].second)) --last;
    if (first == last) return "";
    const auto begin = points[first].first;
    const auto end = last < points.size() ? points[last].first : value.size();
    return value.substr(begin, end - begin);
}

std::string ascii_case(std::string value, const bool upper, const std::string& label) {
    for (auto& c : value) {
        if (static_cast<unsigned char>(c) >= 0x80) fail(label + " must be ASCII in the native importer");
        if (upper && c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
        if (!upper && c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return value;
}

// int(value) for ASCII input: optional sign, digits with single '_' separators.
std::int64_t python_int(const std::string& value, const std::string& label) {
    std::size_t i = 0;
    bool negative = false;
    if (i < value.size() && (value[i] == '+' || value[i] == '-')) negative = value[i++] == '-';
    if (i >= value.size()) fail(label + " must be an integer minor-unit value");
    std::int64_t result = 0;
    bool previous_digit = false;
    for (; i < value.size(); ++i) {
        const char c = value[i];
        if (c == '_' && previous_digit && i + 1 < value.size() && value[i + 1] >= '0' && value[i + 1] <= '9') {
            previous_digit = false;
            continue;
        }
        if (c < '0' || c > '9') fail(label + " must be an integer minor-unit value");
        if (__builtin_mul_overflow(result, 10, &result) || __builtin_add_overflow(result, c - '0', &result)) {
            fail(label + " is outside the supported 64-bit range");
        }
        previous_digit = true;
    }
    return negative ? -result : result;
}

std::string parse_utc(const std::string& value, const std::string& label) {
    const bool z = !value.empty() && value.back() == 'Z';
    if (value.empty() || (!z && value.find('+') == std::string::npos &&
                          (value.size() <= 10 || value.find('-', 10) == std::string::npos))) {
        fail(label + " must include an explicit timezone");
    }
    const auto parsed = parse_timestamp(value);
    if (!parsed.value) {
        if (parsed.problem == TimestampProblem::naive) fail("timestamps must be timezone-aware");
        fail("invalid timestamp: '" + value + "'");
    }
    return parsed.value->text;
}

}  // namespace

Json import_csv(const std::string_view csv_text, const CsvImportOptions& options) {
    if (options.source_id.empty()) fail("source_id is required");
    const auto imported = parse_utc(options.imported_at, "imported_at");
    if (options.source_version < 1) fail("source_version must be positive");
    if (!valid_utf8(csv_text)) fail("CSV is not valid UTF-8");

    auto rows = parse_csv(csv_text);
    // DictReader takes the first row as the header even when it is blank, then
    // skips blank data rows.
    std::vector<std::string> header = rows.empty() ? std::vector<std::string>{} : rows.front();
    std::vector<std::vector<std::string>> nonempty{header};
    for (std::size_t i = 1; i < rows.size(); ++i) {
        if (!rows[i].empty()) nonempty.push_back(std::move(rows[i]));
    }
    std::set<std::string> columns(header.begin(), header.end());
    std::string missing;
    for (const auto& name : k_required) {
        if (!columns.contains(name)) missing += (missing.empty() ? "" : ", ") + name;
    }
    if (!missing.empty()) fail("CSV is missing columns: " + missing);

    std::map<std::string, Json> accounts;
    std::set<std::string> seen;
    Json::Array transactions;
    // csv.DictReader numbers data rows from line 2 regardless of blank lines.
    for (std::size_t r = 1; r < nonempty.size(); ++r) {
        const auto line = std::to_string(r + 1);
        Json row = Json::object();
        for (std::size_t c = 0; c < header.size(); ++c) {
            row.set(header[c], Json::string(c < nonempty[r].size() ? py_strip(nonempty[r][c]) : ""));
        }
        const auto field = [&](const char* name) { return row.get(name).as_string(); };
        const auto account_id = field("account_id");
        const auto transaction_id = field("transaction_id");
        const auto currency = ascii_case(field("currency"), true, "line " + line + ": currency");
        if (account_id.empty() || transaction_id.empty()) fail("line " + line + ": account and transaction IDs are required");
        if (!seen.insert(transaction_id).second) fail("line " + line + ": duplicate transaction_id '" + transaction_id + "'");
        if (!k_currencies.contains(currency)) fail("line " + line + ": unsupported currency '" + currency + "'");
        const auto direction = ascii_case(field("direction"), false, "line " + line + ": direction");
        const auto state = ascii_case(field("state"), false, "line " + line + ": state");
        if (!k_directions.contains(direction)) fail("line " + line + ": invalid direction '" + direction + "'");
        if (!k_states.contains(state)) fail("line " + line + ": invalid state '" + state + "'");
        if (const auto it = accounts.find(account_id);
            it != accounts.end() && it->second.get("currency") != Json::string(currency)) {
            fail("line " + line + ": account currency changed within import");
        }
        const auto observed_at = parse_utc(field("observed_at"), "observed_at");
        const auto content_hash = sha256_hex(canonical_json(row));
        accounts.try_emplace(account_id, Json::object({
                                             {"account_id", Json::string(account_id)},
                                             {"source_id", Json::string(options.source_id)},
                                             {"display_name", Json::string(field("account_name"))},
                                             {"account_type", Json::string(field("account_type"))},
                                             {"currency", Json::string(currency)},
                                         }));
        const auto amount = python_int(field("amount_minor"), "amount_minor");
        if (amount < 0) fail("amount_minor cannot be negative");
        transactions.push_back(Json::object({
            {"transaction_id", Json::string(transaction_id)},
            {"account_id", Json::string(account_id)},
            {"source_transaction_id", Json::string(transaction_id)},
            {"revision", Json::integer(1)},
            {"amount", Json::object({{"minor_units", Json::integer(amount)}, {"currency", Json::string(currency)}})},
            {"direction", Json::string(direction)},
            {"transaction_state", Json::string(state)},
            {"merchant_raw", row.get("merchant_raw", Json::string(""))},
            {"description", Json::string(field("description"))},
            {"observed_at", Json::string(observed_at)},
            {"source_ref", Json::object({
                               {"source_id", Json::string(options.source_id)},
                               {"source_kind", Json::string("csv")},
                               {"external_object_id", Json::string(transaction_id)},
                               {"imported_at", Json::string(imported)},
                               {"observed_at", Json::string(observed_at)},
                               {"content_hash", Json::string("sha256:" + content_hash)},
                               {"source_version", Json::integer(options.source_version)},
                           })},
        }));
    }
    Json::Array account_list;
    for (const auto& [id, account] : accounts) account_list.push_back(account);  // sorted(accounts)
    return Json::object({
        {"schema", Json::string("bundle-connector-1")},
        {"connector_id", Json::string("csv")},
        {"started_at", Json::string(imported)},
        {"completed_at", Json::string(imported)},
        {"accounts", Json::array(std::move(account_list))},
        {"balances", Json::array()},
        {"transactions", Json::array(std::move(transactions))},
        {"external_commitments", Json::array()},
        {"next_cursor", Json{}},
        {"warnings", Json::array()},
    });
}

}  // namespace bundle::persistence
