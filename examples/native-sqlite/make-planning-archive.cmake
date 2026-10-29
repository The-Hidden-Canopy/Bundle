# Packs examples/native-sqlite/planning-records/*.json into a bundle-finance-1
# archive (synthetic planning data: two commitments and one protected goal).
#
#   cmake -DOUT=<new directory> -P examples/native-sqlite/make-planning-archive.cmake
#
# The JSONL files are empty and the manifest is computed here, because the
# repository's public-boundary gate does not allow committed *.jsonl files.
# The output is byte-identical to the Python reference writer (archive.write_export)
# for the same records; tests/parity checks that.

if(NOT OUT)
    message(FATAL_ERROR "usage: cmake -DOUT=<new directory> -P make-planning-archive.cmake")
endif()
if(EXISTS "${OUT}")
    message(FATAL_ERROR "${OUT} already exists; choose a new directory")
endif()
set(RECORDS "${CMAKE_CURRENT_LIST_DIR}/planning-records")
set(CREATED_AT "2026-01-04T12:00:00Z")

file(MAKE_DIRECTORY "${OUT}")
set(entries "")
foreach(name accounts.json balances.jsonl transactions.jsonl commitments.json budgets.json goals.json
        overrides.jsonl receipts.jsonl)
    if(name MATCHES "\\.jsonl$")
        file(WRITE "${OUT}/${name}" "")
    else()
        file(COPY_FILE "${RECORDS}/${name}" "${OUT}/${name}")
    endif()
    file(SIZE "${OUT}/${name}" bytes)
    file(SHA256 "${OUT}/${name}" digest)
    if(NOT entries STREQUAL "")
        string(APPEND entries ",")
    endif()
    string(APPEND entries "{\"bytes\":${bytes},\"path\":\"${name}\",\"sha256\":\"${digest}\"}")
endforeach()
# file(WRITE) would emit CRLF on Windows; the manifest must end in a single LF.
set(MANIFEST "{\"created_at\":\"${CREATED_AT}\",\"files\":[${entries}],\"schema\":\"bundle-finance-1\"}")
file(CONFIGURE OUTPUT "${OUT}/manifest.json" CONTENT "@MANIFEST@\n" @ONLY NEWLINE_STYLE UNIX)
message(STATUS "wrote bundle-finance-1 planning archive: ${OUT}")
