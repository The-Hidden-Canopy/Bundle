#pragma once

// Native port of connectors/bundle_connectors/csv_import.py: converts the
// canonical BUNDLE CSV into the same bundle-connector-1 batch document
// (ConnectorBatch.to_dict()) so both paths hash and admit identical batches.

#include "bundle/persistence/json.hpp"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace bundle::persistence {

class CsvImportError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct CsvImportOptions final {
    std::string source_id;
    std::string imported_at;  // RFC 3339 with explicit offset
    std::int64_t source_version = 1;
};

[[nodiscard]] Json import_csv(std::string_view csv_text, const CsvImportOptions& options);

}  // namespace bundle::persistence
