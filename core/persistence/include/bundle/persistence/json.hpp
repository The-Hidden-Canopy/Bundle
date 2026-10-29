#pragma once

// JSON with the exact semantics the Python reference bridge relies on.
//
// The SQLite bridge hashes and persists JSON produced by Python's `json`
// module, so byte-compatibility matters more than generality:
//   * parse() accepts what `json.loads` accepts (including NaN/Infinity and
//     duplicate keys, last value wins) and rejects lone surrogates and
//     integers outside int64 with an explicit error instead of guessing;
//   * dump() reproduces `json.dumps(value, sort_keys=..., separators=(",", ":"),
//     ensure_ascii=...)`, including Python's float repr.

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace bundle::persistence {

class JsonError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class Json final {
public:
    enum class Kind { null, boolean, integer, real, string, array, object };
    using Array = std::vector<Json>;
    using Object = std::vector<std::pair<std::string, Json>>;  // insertion order

    Json() = default;
    static Json null() { return Json{}; }
    static Json boolean(bool value);
    static Json integer(std::int64_t value);
    static Json real(double value);
    static Json string(std::string value);
    static Json array(Array values = {});
    static Json object(Object members = {});

    [[nodiscard]] Kind kind() const noexcept { return kind_; }
    [[nodiscard]] bool is_null() const noexcept { return kind_ == Kind::null; }
    [[nodiscard]] bool is_bool() const noexcept { return kind_ == Kind::boolean; }
    // Python `isinstance(x, int) and not isinstance(x, bool)`.
    [[nodiscard]] bool is_int() const noexcept { return kind_ == Kind::integer; }
    [[nodiscard]] bool is_real() const noexcept { return kind_ == Kind::real; }
    [[nodiscard]] bool is_string() const noexcept { return kind_ == Kind::string; }
    [[nodiscard]] bool is_array() const noexcept { return kind_ == Kind::array; }
    [[nodiscard]] bool is_object() const noexcept { return kind_ == Kind::object; }

    [[nodiscard]] bool as_bool() const;
    [[nodiscard]] std::int64_t as_int() const;  // bool counts as int (Python semantics)
    [[nodiscard]] double as_real() const;
    [[nodiscard]] const std::string& as_string() const;
    [[nodiscard]] const Array& as_array() const;
    [[nodiscard]] Array& as_array();
    [[nodiscard]] const Object& as_object() const;
    [[nodiscard]] Object& as_object();

    // Object helpers (Python dict semantics).
    [[nodiscard]] bool contains(std::string_view key) const;
    [[nodiscard]] const Json* find(std::string_view key) const;
    // value.get(key) -> None when absent.
    [[nodiscard]] Json get(std::string_view key) const;
    // value.get(key, default)
    [[nodiscard]] Json get(std::string_view key, const Json& fallback) const;
    void set(std::string key, Json value);  // dict[key] = value
    bool erase(std::string_view key);       // dict.pop(key, None) is not None-checking

    // Python truthiness (used for `a or b`).
    [[nodiscard]] bool truthy() const;

    friend bool operator==(const Json& left, const Json& right);

private:
    Kind kind_ = Kind::null;
    bool bool_ = false;
    std::int64_t int_ = 0;
    double real_ = 0.0;
    std::string string_;
    std::shared_ptr<Array> array_;
    std::shared_ptr<Object> object_;
};

[[nodiscard]] Json parse_json(std::string_view text);

struct DumpOptions final {
    bool sort_keys = true;
    bool ensure_ascii = false;
};

[[nodiscard]] std::string dump_json(const Json& value, DumpOptions options = {});

// json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False)
[[nodiscard]] inline std::string canonical_json(const Json& value) {
    return dump_json(value, DumpOptions{.sort_keys = true, .ensure_ascii = false});
}

// Python float.__repr__.
[[nodiscard]] std::string python_float_repr(double value);

// Python str(value) for scalar JSON values; containers are rejected because
// their Python repr is not a stable persisted format.
[[nodiscard]] std::optional<std::string> python_str(const Json& value);

// Validates that bytes are well-formed UTF-8 (Python strict decoding).
[[nodiscard]] bool valid_utf8(std::string_view bytes) noexcept;

}  // namespace bundle::persistence
