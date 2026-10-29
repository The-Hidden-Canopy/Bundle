#include "bundle/persistence/json.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace bundle::persistence {

Json Json::boolean(const bool value) {
    Json json;
    json.kind_ = Kind::boolean;
    json.bool_ = value;
    return json;
}

Json Json::integer(const std::int64_t value) {
    Json json;
    json.kind_ = Kind::integer;
    json.int_ = value;
    return json;
}

Json Json::real(const double value) {
    Json json;
    json.kind_ = Kind::real;
    json.real_ = value;
    return json;
}

Json Json::string(std::string value) {
    Json json;
    json.kind_ = Kind::string;
    json.string_ = std::move(value);
    return json;
}

Json Json::array(Array values) {
    Json json;
    json.kind_ = Kind::array;
    json.array_ = std::make_shared<Array>(std::move(values));
    return json;
}

Json Json::object(Object members) {
    Json json;
    json.kind_ = Kind::object;
    json.object_ = std::make_shared<Object>(std::move(members));
    return json;
}

bool Json::as_bool() const {
    if (kind_ != Kind::boolean) {
        throw JsonError("JSON value is not a boolean");
    }
    return bool_;
}

std::int64_t Json::as_int() const {
    if (kind_ == Kind::boolean) {
        return bool_ ? 1 : 0;
    }
    if (kind_ != Kind::integer) {
        throw JsonError("JSON value is not an integer");
    }
    return int_;
}

double Json::as_real() const {
    if (kind_ == Kind::real) {
        return real_;
    }
    if (kind_ == Kind::integer) {
        return static_cast<double>(int_);
    }
    throw JsonError("JSON value is not a number");
}

const std::string& Json::as_string() const {
    if (kind_ != Kind::string) {
        throw JsonError("JSON value is not a string");
    }
    return string_;
}

const Json::Array& Json::as_array() const {
    if (kind_ != Kind::array) {
        throw JsonError("JSON value is not an array");
    }
    return *array_;
}

Json::Array& Json::as_array() {
    if (kind_ != Kind::array) {
        throw JsonError("JSON value is not an array");
    }
    if (array_.use_count() > 1) {
        array_ = std::make_shared<Array>(*array_);
    }
    return *array_;
}

const Json::Object& Json::as_object() const {
    if (kind_ != Kind::object) {
        throw JsonError("JSON value is not an object");
    }
    return *object_;
}

Json::Object& Json::as_object() {
    if (kind_ != Kind::object) {
        throw JsonError("JSON value is not an object");
    }
    if (object_.use_count() > 1) {
        object_ = std::make_shared<Object>(*object_);
    }
    return *object_;
}

bool Json::contains(const std::string_view key) const { return find(key) != nullptr; }

const Json* Json::find(const std::string_view key) const {
    if (kind_ != Kind::object) {
        return nullptr;
    }
    for (const auto& [name, value] : *object_) {
        if (name == key) {
            return &value;
        }
    }
    return nullptr;
}

Json Json::get(const std::string_view key) const {
    const auto* value = find(key);
    return value != nullptr ? *value : Json{};
}

Json Json::get(const std::string_view key, const Json& fallback) const {
    const auto* value = find(key);
    return value != nullptr ? *value : fallback;
}

void Json::set(std::string key, Json value) {
    auto& members = as_object();
    for (auto& [name, existing] : members) {
        if (name == key) {
            existing = std::move(value);
            return;
        }
    }
    members.emplace_back(std::move(key), std::move(value));
}

bool Json::erase(const std::string_view key) {
    auto& members = as_object();
    const auto it = std::find_if(members.begin(), members.end(),
                                 [&](const auto& member) { return member.first == key; });
    if (it == members.end()) {
        return false;
    }
    members.erase(it);
    return true;
}

bool Json::truthy() const {
    switch (kind_) {
        case Kind::null: return false;
        case Kind::boolean: return bool_;
        case Kind::integer: return int_ != 0;
        case Kind::real: return real_ != 0.0;
        case Kind::string: return !string_.empty();
        case Kind::array: return !array_->empty();
        case Kind::object: return !object_->empty();
    }
    return false;
}

namespace {

bool numeric(const Json& value) {
    return value.kind() == Json::Kind::boolean || value.kind() == Json::Kind::integer ||
           value.kind() == Json::Kind::real;
}

}  // namespace

bool operator==(const Json& left, const Json& right) {
    // Python: True == 1 == 1.0, but "1" != 1 and None only equals None.
    if (numeric(left) && numeric(right)) {
        if (left.kind() == Json::Kind::real || right.kind() == Json::Kind::real) {
            return left.as_real() == right.as_real();
        }
        return left.as_int() == right.as_int();
    }
    if (left.kind() != right.kind()) {
        return false;
    }
    switch (left.kind()) {
        case Json::Kind::null: return true;
        case Json::Kind::string: return left.string_ == right.string_;
        case Json::Kind::array: return *left.array_ == *right.array_;
        case Json::Kind::object: {
            if (left.object_->size() != right.object_->size()) {
                return false;
            }
            for (const auto& [key, value] : *left.object_) {
                const auto* other = right.find(key);
                if (other == nullptr || !(*other == value)) {
                    return false;
                }
            }
            return true;
        }
        default: return false;
    }
}

// -- UTF-8 ----------------------------------------------------------------------

namespace {

// Decodes one code point; returns false on malformed input.
bool decode_utf8(const std::string_view text, std::size_t& index, char32_t& out) noexcept {
    const auto byte = static_cast<unsigned char>(text[index]);
    if (byte < 0x80) {
        out = byte;
        ++index;
        return true;
    }
    std::size_t length = 0;
    char32_t value = 0;
    char32_t minimum = 0;
    if ((byte & 0xE0) == 0xC0) {
        length = 2; value = byte & 0x1F; minimum = 0x80;
    } else if ((byte & 0xF0) == 0xE0) {
        length = 3; value = byte & 0x0F; minimum = 0x800;
    } else if ((byte & 0xF8) == 0xF0) {
        length = 4; value = byte & 0x07; minimum = 0x10000;
    } else {
        return false;
    }
    if (index + length > text.size()) {
        return false;
    }
    for (std::size_t offset = 1; offset < length; ++offset) {
        const auto next = static_cast<unsigned char>(text[index + offset]);
        if ((next & 0xC0) != 0x80) {
            return false;
        }
        value = (value << 6) | (next & 0x3F);
    }
    if (value < minimum || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) {
        return false;
    }
    out = value;
    index += length;
    return true;
}

void append_utf8(std::string& out, const char32_t code) {
    if (code < 0x80) {
        out.push_back(static_cast<char>(code));
    } else if (code < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (code >> 6)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else if (code < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (code >> 12)));
        out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (code >> 18)));
        out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    }
}

}  // namespace

bool valid_utf8(const std::string_view bytes) noexcept {
    std::size_t index = 0;
    char32_t code = 0;
    while (index < bytes.size()) {
        if (!decode_utf8(bytes, index, code)) {
            return false;
        }
    }
    return true;
}

// -- parser ---------------------------------------------------------------------

namespace {

class Parser final {
public:
    explicit Parser(const std::string_view text) : text_(text) {}

    Json document() {
        if (!valid_utf8(text_)) {
            throw JsonError("JSON text is not valid UTF-8");
        }
        skip();
        auto value = parse_value(0);
        skip();
        if (position_ != text_.size()) {
            fail("extra data after JSON value");
        }
        return value;
    }

private:
    [[noreturn]] void fail(const std::string& message) const {
        throw JsonError(message + " at offset " + std::to_string(position_));
    }

    void skip() {
        while (position_ < text_.size()) {
            const char c = text_[position_];
            if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
                break;
            }
            ++position_;
        }
    }

    bool literal(const std::string_view word) {
        if (text_.substr(position_, word.size()) == word) {
            position_ += word.size();
            return true;
        }
        return false;
    }

    Json parse_value(const int depth) {
        if (depth > 512) {
            fail("JSON nesting is too deep");
        }
        if (position_ >= text_.size()) {
            fail("unexpected end of JSON");
        }
        const char c = text_[position_];
        if (c == '{') return parse_object(depth);
        if (c == '[') return parse_array(depth);
        if (c == '"') return Json::string(parse_string());
        if (literal("null")) return Json{};
        if (literal("true")) return Json::boolean(true);
        if (literal("false")) return Json::boolean(false);
        if (literal("NaN")) return Json::real(std::numeric_limits<double>::quiet_NaN());
        if (literal("Infinity")) return Json::real(std::numeric_limits<double>::infinity());
        if (literal("-Infinity")) return Json::real(-std::numeric_limits<double>::infinity());
        if (c == '-' || (c >= '0' && c <= '9')) return parse_number();
        fail("unexpected character in JSON");
    }

    Json parse_object(const int depth) {
        ++position_;
        Json object = Json::object();
        skip();
        if (position_ < text_.size() && text_[position_] == '}') {
            ++position_;
            return object;
        }
        for (;;) {
            skip();
            if (position_ >= text_.size() || text_[position_] != '"') {
                fail("expected property name");
            }
            auto key = parse_string();
            skip();
            if (position_ >= text_.size() || text_[position_] != ':') {
                fail("expected ':'");
            }
            ++position_;
            skip();
            auto value = parse_value(depth + 1);
            object.set(std::move(key), std::move(value));  // duplicate key: last wins
            skip();
            if (position_ < text_.size() && text_[position_] == ',') {
                ++position_;
                continue;
            }
            if (position_ < text_.size() && text_[position_] == '}') {
                ++position_;
                return object;
            }
            fail("expected ',' or '}'");
        }
    }

    Json parse_array(const int depth) {
        ++position_;
        Json::Array values;
        skip();
        if (position_ < text_.size() && text_[position_] == ']') {
            ++position_;
            return Json::array(std::move(values));
        }
        for (;;) {
            skip();
            values.push_back(parse_value(depth + 1));
            skip();
            if (position_ < text_.size() && text_[position_] == ',') {
                ++position_;
                continue;
            }
            if (position_ < text_.size() && text_[position_] == ']') {
                ++position_;
                return Json::array(std::move(values));
            }
            fail("expected ',' or ']'");
        }
    }

    unsigned hex4() {
        if (position_ + 4 > text_.size()) {
            fail("invalid \\u escape");
        }
        unsigned value = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = text_[position_++];
            value <<= 4;
            if (c >= '0' && c <= '9') value |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') value |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') value |= static_cast<unsigned>(c - 'A' + 10);
            else fail("invalid \\u escape");
        }
        return value;
    }

    std::string parse_string() {
        ++position_;  // opening quote
        std::string out;
        for (;;) {
            if (position_ >= text_.size()) {
                fail("unterminated string");
            }
            const auto c = static_cast<unsigned char>(text_[position_]);
            if (c == '"') {
                ++position_;
                return out;
            }
            if (c < 0x20) {
                fail("invalid control character in string");
            }
            if (c != '\\') {
                out.push_back(static_cast<char>(c));
                ++position_;
                continue;
            }
            ++position_;
            if (position_ >= text_.size()) {
                fail("unterminated escape");
            }
            const char escape = text_[position_++];
            switch (escape) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    char32_t code = hex4();
                    if (code >= 0xD800 && code <= 0xDBFF) {
                        if (text_.substr(position_, 2) != "\\u") {
                            fail("lone surrogate in string is not supported");
                        }
                        position_ += 2;
                        const char32_t low = hex4();
                        if (low < 0xDC00 || low > 0xDFFF) {
                            fail("lone surrogate in string is not supported");
                        }
                        code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                    } else if (code >= 0xDC00 && code <= 0xDFFF) {
                        fail("lone surrogate in string is not supported");
                    }
                    append_utf8(out, code);
                    break;
                }
                default: fail("invalid escape");
            }
        }
    }

    Json parse_number() {
        const std::size_t start = position_;
        if (text_[position_] == '-') {
            ++position_;
        }
        if (position_ >= text_.size()) {
            fail("invalid number");
        }
        if (text_[position_] == '0') {
            ++position_;
        } else if (text_[position_] >= '1' && text_[position_] <= '9') {
            while (position_ < text_.size() && text_[position_] >= '0' && text_[position_] <= '9') ++position_;
        } else {
            fail("invalid number");
        }
        bool is_real = false;
        if (position_ + 1 < text_.size() && text_[position_] == '.' &&
            text_[position_ + 1] >= '0' && text_[position_ + 1] <= '9') {
            is_real = true;
            ++position_;
            while (position_ < text_.size() && text_[position_] >= '0' && text_[position_] <= '9') ++position_;
        }
        if (position_ < text_.size() && (text_[position_] == 'e' || text_[position_] == 'E')) {
            std::size_t probe = position_ + 1;
            if (probe < text_.size() && (text_[probe] == '+' || text_[probe] == '-')) ++probe;
            if (probe < text_.size() && text_[probe] >= '0' && text_[probe] <= '9') {
                is_real = true;
                position_ = probe;
                while (position_ < text_.size() && text_[position_] >= '0' && text_[position_] <= '9') ++position_;
            }
        }
        const auto token = text_.substr(start, position_ - start);
        if (!is_real) {
            std::int64_t value = 0;
            const auto result = std::from_chars(token.data(), token.data() + token.size(), value);
            if (result.ec != std::errc{}) {
                throw JsonError("integer " + std::string(token) +
                                " is outside the supported 64-bit range");
            }
            return Json::integer(value);
        }
        // strtod matches Python float() for decimal input, including overflow to inf.
        const std::string copy(token);
        return Json::real(std::strtod(copy.c_str(), nullptr));
    }

    std::string_view text_;
    std::size_t position_ = 0;
};

}  // namespace

Json parse_json(const std::string_view text) { return Parser(text).document(); }

// -- dumper ---------------------------------------------------------------------

std::string python_float_repr(const double value) {
    if (std::isnan(value)) return "NaN";
    if (std::isinf(value)) return value > 0 ? "Infinity" : "-Infinity";
    if (value == 0.0) return std::signbit(value) ? "-0.0" : "0.0";
    // Shortest round-trip digits, then Python's 'r' layout rules.
    char buffer[64];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value,
                                      std::chars_format::scientific);
    std::string sci(buffer, result.ptr);
    const bool negative = sci[0] == '-';
    if (negative) sci.erase(0, 1);
    const auto e_pos = sci.find('e');
    std::string mantissa = sci.substr(0, e_pos);
    const int exponent = std::stoi(sci.substr(e_pos + 1));
    std::string digits;
    for (const char c : mantissa) {
        if (c != '.') digits.push_back(c);
    }
    while (digits.size() > 1 && digits.back() == '0') digits.pop_back();
    std::string out = negative ? "-" : "";
    if (exponent >= -4 && exponent < 16) {
        if (exponent < 0) {
            out += "0." + std::string(static_cast<std::size_t>(-exponent - 1), '0') + digits;
        } else {
            const auto integer_digits = static_cast<std::size_t>(exponent + 1);
            if (digits.size() <= integer_digits) {
                out += digits + std::string(integer_digits - digits.size(), '0') + ".0";
            } else {
                out += digits.substr(0, integer_digits) + "." + digits.substr(integer_digits);
            }
        }
    } else {
        out += digits.substr(0, 1);
        if (digits.size() > 1) out += "." + digits.substr(1);
        char exp_text[16];
        std::snprintf(exp_text, sizeof(exp_text), "e%c%02d", exponent < 0 ? '-' : '+',
                      exponent < 0 ? -exponent : exponent);
        out += exp_text;
    }
    return out;
}

namespace {

void hex_escape(std::string& out, const unsigned code) {
    char buffer[8];
    std::snprintf(buffer, sizeof(buffer), "\\u%04x", code);
    out += buffer;
}

void dump_string(std::string& out, const std::string& value, const bool ensure_ascii) {
    out.push_back('"');
    std::size_t index = 0;
    while (index < value.size()) {
        char32_t code = 0;
        const std::size_t start = index;
        if (!decode_utf8(value, index, code)) {
            throw JsonError("string is not valid UTF-8");
        }
        switch (code) {
            case '"': out += "\\\""; continue;
            case '\\': out += "\\\\"; continue;
            case '\n': out += "\\n"; continue;
            case '\r': out += "\\r"; continue;
            case '\t': out += "\\t"; continue;
            case '\b': out += "\\b"; continue;
            case '\f': out += "\\f"; continue;
            default: break;
        }
        if (code < 0x20) {
            hex_escape(out, static_cast<unsigned>(code));
        } else if (ensure_ascii && (code < 0x20 || code > 0x7E)) {
            if (code >= 0x10000) {
                const char32_t shifted = code - 0x10000;
                hex_escape(out, static_cast<unsigned>(0xD800 + (shifted >> 10)));
                hex_escape(out, static_cast<unsigned>(0xDC00 + (shifted & 0x3FF)));
            } else {
                hex_escape(out, static_cast<unsigned>(code));
            }
        } else {
            out.append(value, start, index - start);
        }
    }
    out.push_back('"');
}

void dump_value(std::string& out, const Json& value, const DumpOptions& options) {
    switch (value.kind()) {
        case Json::Kind::null: out += "null"; return;
        case Json::Kind::boolean: out += value.as_bool() ? "true" : "false"; return;
        case Json::Kind::integer: out += std::to_string(value.as_int()); return;
        case Json::Kind::real: out += python_float_repr(value.as_real()); return;
        case Json::Kind::string: dump_string(out, value.as_string(), options.ensure_ascii); return;
        case Json::Kind::array: {
            out.push_back('[');
            bool first = true;
            for (const auto& item : value.as_array()) {
                if (!first) out.push_back(',');
                first = false;
                dump_value(out, item, options);
            }
            out.push_back(']');
            return;
        }
        case Json::Kind::object: {
            std::vector<const std::pair<std::string, Json>*> members;
            for (const auto& member : value.as_object()) members.push_back(&member);
            if (options.sort_keys) {
                // Python sorts str keys by code point; UTF-8 byte order is equivalent.
                std::stable_sort(members.begin(), members.end(),
                                 [](const auto* a, const auto* b) { return a->first < b->first; });
            }
            out.push_back('{');
            bool first = true;
            for (const auto* member : members) {
                if (!first) out.push_back(',');
                first = false;
                dump_string(out, member->first, options.ensure_ascii);
                out.push_back(':');
                dump_value(out, member->second, options);
            }
            out.push_back('}');
            return;
        }
    }
}

}  // namespace

std::string dump_json(const Json& value, const DumpOptions options) {
    std::string out;
    dump_value(out, value, options);
    return out;
}

std::optional<std::string> python_str(const Json& value) {
    switch (value.kind()) {
        case Json::Kind::null: return std::string("None");
        case Json::Kind::boolean: return std::string(value.as_bool() ? "True" : "False");
        case Json::Kind::integer: return std::to_string(value.as_int());
        case Json::Kind::real: {
            const double number = value.as_real();
            if (std::isnan(number)) return std::string("nan");
            if (std::isinf(number)) return std::string(number > 0 ? "inf" : "-inf");
            return python_float_repr(number);
        }
        case Json::Kind::string: return value.as_string();
        default: return std::nullopt;
    }
}

}  // namespace bundle::persistence
