// Byte-level compatibility of the primitives the Python bridge hashes and persists.
// Expected strings were produced with CPython 3.12's json/hashlib/datetime.

#include "bundle/persistence/archive.hpp"
#include "bundle/persistence/csv_import.hpp"
#include "bundle/persistence/json.hpp"
#include "bundle/persistence/text.hpp"
#include "bundle_test/check.hpp"

#include <limits>

using namespace bundle::persistence;

namespace {

void json_matches_python_dumps() {
    const auto value = parse_json("{\"b\":[1,2.5,true,null],\"a\":\"caf\\u00e9 \\u2615 \\ud83d\\ude00\",\"c\":{\"z\":-0.0,\"y\":1e16}}");
    CHECK_EQ(canonical_json(value),
             std::string("{\"a\":\"caf\xC3\xA9 \xE2\x98\x95 \xF0\x9F\x98\x80\",\"b\":[1,2.5,true,null],\"c\":{\"y\":1e+16,\"z\":-0.0}}"));
    CHECK_EQ(dump_json(value, {.sort_keys = true, .ensure_ascii = true}),
             std::string("{\"a\":\"caf\\u00e9 \\u2615 \\ud83d\\ude00\",\"b\":[1,2.5,true,null],\"c\":{\"y\":1e+16,\"z\":-0.0}}"));
    // Control characters and DEL: Python escapes < 0x20 always, DEL only with ensure_ascii.
    const auto controls = Json::string(std::string("a\x01\b\f\n\r\t\x7f\"\\", 10));
    CHECK_EQ(canonical_json(controls), std::string("\"a\\u0001\\b\\f\\n\\r\\t\x7f\\\"\\\\\""));
    CHECK_EQ(dump_json(controls, {.sort_keys = true, .ensure_ascii = true}),
             std::string("\"a\\u0001\\b\\f\\n\\r\\t\\u007f\\\"\\\\\""));
    // Python float repr.
    CHECK_EQ(python_float_repr(0.1), std::string("0.1"));
    CHECK_EQ(python_float_repr(1.0), std::string("1.0"));
    CHECK_EQ(python_float_repr(1e16), std::string("1e+16"));
    CHECK_EQ(python_float_repr(123456789012345.0), std::string("123456789012345.0"));
    CHECK_EQ(python_float_repr(1e-5), std::string("1e-05"));
    CHECK_EQ(python_float_repr(0.0001), std::string("0.0001"));
    CHECK_EQ(python_float_repr(1.5e300), std::string("1.5e+300"));
    CHECK_EQ(python_float_repr(-2.5), std::string("-2.5"));
    CHECK_EQ(canonical_json(parse_json("[NaN,Infinity,-Infinity]")), std::string("[NaN,Infinity,-Infinity]"));
    // Python semantics for equality and str().
    CHECK(Json::integer(1) == Json::real(1.0));
    CHECK(Json::boolean(true) == Json::integer(1));
    CHECK(!(Json::string("1") == Json::integer(1)));
    CHECK_EQ(*python_str(Json{}), std::string("None"));
    CHECK_EQ(*python_str(Json::boolean(false)), std::string("False"));
    // Duplicate keys: last value wins (json.loads).
    CHECK_EQ(canonical_json(parse_json(R"({"a":1,"a":2})")), std::string(R"({"a":2})"));
    // Rejections with explicit errors.
    CHECK_THROWS(parse_json("\"\\ud800\""), JsonError);
    CHECK_THROWS(parse_json("99999999999999999999"), JsonError);
    CHECK_THROWS(parse_json("[1,]"), JsonError);
    CHECK_THROWS(parse_json("\"a\x01\""), JsonError);
    CHECK_THROWS(parse_json("\xEF\xBB\xBF[]"), JsonError);  // UTF-8 BOM, like json.loads
}

void hashes() {
    CHECK_EQ(sha256_hex(""), std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    CHECK_EQ(sha256_hex("abc"), std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    CHECK_EQ(sha256_hex(std::string(1000, 'a')),
             std::string("41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3"));
    CHECK(is_sha256_digest("sha256:" + std::string(64, 'A')));
    CHECK(!is_sha256_digest("sha256:short"));
}

void timestamps() {
    const auto normalized = [](const char* text) {
        const auto parsed = parse_timestamp(text);
        return parsed.value ? parsed.value->text : std::string("<rejected>");
    };
    CHECK_EQ(normalized("2026-01-01T00:00:00Z"), std::string("2026-01-01T00:00:00Z"));
    CHECK_EQ(normalized("2026-01-01 00:00:00+00:00"), std::string("2026-01-01T00:00:00Z"));
    CHECK_EQ(normalized("2026-01-01T00Z"), std::string("2026-01-01T00:00:00Z"));
    CHECK_EQ(normalized("2026-01-01T00:00:00.5Z"), std::string("2026-01-01T00:00:00.500000Z"));
    CHECK_EQ(normalized("2026-01-01T00:00:00.1234567Z"), std::string("2026-01-01T00:00:00.123456Z"));
    CHECK_EQ(normalized("2026-01-01T00:00:00,5Z"), std::string("2026-01-01T00:00:00.500000Z"));
    CHECK_EQ(normalized("2026-01-01T00:00:00.000000Z"), std::string("2026-01-01T00:00:00Z"));
    CHECK_EQ(normalized("2026-01-01T00:00:00-08:00"), std::string("2026-01-01T08:00:00Z"));
    CHECK_EQ(normalized("2026-01-01T00:00:00+0530"), std::string("2025-12-31T18:30:00Z"));
    CHECK_EQ(normalized("2026-01-01T00:00:00+05"), std::string("2025-12-31T19:00:00Z"));
    CHECK_EQ(normalized("2026-01-01T00:00:00+05:30:15.5"), std::string("2025-12-31T18:29:44.500000Z"));
    CHECK_EQ(normalized("20260101T000000Z"), std::string("2026-01-01T00:00:00Z"));
    CHECK_EQ(normalized("2026-01-01T00:00:00 Z"), std::string("2026-01-01T00:00:00Z"));
    CHECK(parse_timestamp("2026-01-01T00:00:00").problem == TimestampProblem::naive);
    CHECK(parse_timestamp("2026-01-01").problem == TimestampProblem::naive);
    CHECK(parse_timestamp("0001-01-01T00:00:00+01:00").problem == TimestampProblem::out_of_range);
    for (const char* bad : {"2026-01-01T24:00:00Z", "2026-02-30T00:00:00Z", "2026-01-01T00:00:60Z",
                            "2026-01-01t00:00:00z", "2026-01-01T00:00:00+24:00", "2026-W01-1T00:00:00Z",
                            "2026-01-01T00:00:00+00:00Z", "2026-01-01T00:00:00.Z"}) {
        CHECK(!parse_timestamp(bad).value.has_value());
    }
}

void csv_dialect() {
    const std::string text =
        "account_id,account_name,account_type,currency,transaction_id,amount_minor,direction,state,description,"
        "observed_at\r\n\r\n"
        "a,A,checking,usd,t1,1_000,DEBIT,Posted,\"x, \"\"y\"\"\nz\",2026-01-01T00:00:00Z\n";
    const auto batch = import_csv(text, {"src", "2026-01-02T00:00:00Z", 1});
    const auto tx = batch.get("transactions").as_array().at(0);
    CHECK_EQ(tx.get("description").as_string(), std::string("x, \"y\"\nz"));
    CHECK_EQ(tx.get("amount").get("minor_units").as_int(), std::int64_t{1000});
    CHECK_EQ(tx.get("direction").as_string(), std::string("debit"));
    CHECK_THROWS(import_csv("account_id\nx\n", {"src", "2026-01-02T00:00:00Z", 1}), CsvImportError);
    CHECK_THROWS(import_csv(text, {"src", "2026-01-02T00:00:00", 1}), CsvImportError);
    CHECK_THROWS(import_csv(text + "a,A,checking,usd,t1,1,debit,posted,dup,2026-01-01T00:00:00Z\n",
                            {"src", "2026-01-02T00:00:00Z", 1}), CsvImportError);
}

void jsonl_layout() {
    std::vector<Json> records = {parse_json(R"({"transaction_id":"b","n":1})"),
                                 parse_json(R"({"transaction_id":"a","n":2})")};
    CHECK_EQ(archive_jsonl_bytes(records),
             std::string("{\"n\":2,\"transaction_id\":\"a\"}\n{\"n\":1,\"transaction_id\":\"b\"}\n"));
    CHECK_THROWS((void)archive_json_bytes(parse_json(R"([{"refresh_token":"x"}])")), ArchiveError);
}

}  // namespace

int main() {
    json_matches_python_dumps();
    hashes();
    timestamps();
    csv_dialect();
    jsonl_layout();
    return 0;
}
