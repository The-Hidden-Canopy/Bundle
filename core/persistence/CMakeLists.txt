# Native SQLite persistence adapter (risk register R-003) and the `bundle` CLI.
#
# SQLite comes from the system when available (find_package), otherwise from
# the official amalgamation pinned by its published SHA3-256. Set
# BUNDLE_SQLITE_AMALGAMATION_URL to a local copy of the same zip for offline
# or shared-cache builds; the hash is checked either way.

find_package(SQLite3 QUIET)
if(NOT SQLite3_FOUND)
    set(BUNDLE_SQLITE_AMALGAMATION_URL
        "https://www.sqlite.org/2026/sqlite-amalgamation-3540100.zip"
        CACHE STRING "URL or local path of sqlite-amalgamation-3540100.zip")
    include(FetchContent)
    FetchContent_Declare(bundle_sqlite
        URL "${BUNDLE_SQLITE_AMALGAMATION_URL}"
        URL_HASH SHA3_256=92281c86e2957f9b9305890f9ac04204caa7b677cc868dc326d942689df28e30
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(bundle_sqlite)
    enable_language(C)
    add_library(bundle_sqlite3 STATIC "${bundle_sqlite_SOURCE_DIR}/sqlite3.c")
    target_include_directories(bundle_sqlite3 PUBLIC "${bundle_sqlite_SOURCE_DIR}")
    target_compile_definitions(bundle_sqlite3 PRIVATE SQLITE_DQS=0 SQLITE_OMIT_LOAD_EXTENSION)
    if(NOT MSVC)
        target_compile_options(bundle_sqlite3 PRIVATE -w)
        find_package(Threads REQUIRED)
        target_link_libraries(bundle_sqlite3 PUBLIC Threads::Threads)
    endif()
    add_library(SQLite::SQLite3 ALIAS bundle_sqlite3)
    set(BUNDLE_SQLITE_PROVENANCE "amalgamation 3.54.1 (SHA3-256 pinned)")
else()
    set(BUNDLE_SQLITE_PROVENANCE "system ${SQLite3_VERSION}")
endif()
message(STATUS "BUNDLE native SQLite adapter uses ${BUNDLE_SQLITE_PROVENANCE}")

# Embed the migration as bytes (LF-normalized like Python's Path.read_text()).
set(BUNDLE_MIGRATION "${CMAKE_CURRENT_SOURCE_DIR}/migrations/001_finance.sql")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${BUNDLE_MIGRATION}")
file(READ "${BUNDLE_MIGRATION}" BUNDLE_MIGRATION_HEX HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," BUNDLE_MIGRATION_BYTES "${BUNDLE_MIGRATION_HEX}")
string(REPLACE "0x0d,0x0a," "0x0a," BUNDLE_MIGRATION_BYTES "${BUNDLE_MIGRATION_BYTES}")
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/embedded_migration.cpp" CONTENT [=[
// Generated from core/persistence/migrations/001_finance.sql. Do not edit.
#include "bundle/persistence/sqlite_store.hpp"

namespace bundle::persistence {
namespace {
constexpr unsigned char k_bytes[] = {@BUNDLE_MIGRATION_BYTES@};
}  // namespace

const std::string& SqliteFinanceStore::embedded_migration() {
    static const std::string sql(reinterpret_cast<const char*>(k_bytes), sizeof(k_bytes));
    return sql;
}
}  // namespace bundle::persistence
]=] @ONLY)

add_library(bundle_persistence STATIC
    src/archive.cpp
    src/calculation.cpp
    src/csv_import.cpp
    src/json.cpp
    src/snapshot.cpp
    src/sqlite_store.cpp
    src/text.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/generated/embedded_migration.cpp"
)
add_library(bundle::persistence ALIAS bundle_persistence)
target_compile_features(bundle_persistence PUBLIC cxx_std_20)
target_include_directories(bundle_persistence PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(bundle_persistence PUBLIC bundle::core PRIVATE SQLite::SQLite3)
target_compile_definitions(bundle_persistence PRIVATE
    BUNDLE_PERSISTENCE_VERSION="${PROJECT_VERSION}")
if(MSVC)
    target_compile_options(bundle_persistence PRIVATE /W4 /permissive-)
else()
    target_compile_options(bundle_persistence PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(bundle_cli cli/main.cpp)
set_target_properties(bundle_cli PROPERTIES OUTPUT_NAME bundle)
target_link_libraries(bundle_cli PRIVATE bundle::persistence)
target_compile_definitions(bundle_cli PRIVATE
    BUNDLE_VERSION="${PROJECT_VERSION}"
    BUNDLE_SQLITE_PROVENANCE="${BUNDLE_SQLITE_PROVENANCE}")
if(MINGW)
    # Self-contained binary: no MinGW runtime DLLs needed next to bundle.exe.
    target_link_options(bundle_cli PRIVATE -static)
endif()

if(BUNDLE_BUILD_TESTS)
    add_subdirectory(tests)
endif()
