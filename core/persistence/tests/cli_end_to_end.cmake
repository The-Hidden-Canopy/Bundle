# End-to-end acceptance through the real `bundle` executable, one process per step
# (every step is a cold restart of the application against the same files):
# import -> inspect history -> calculate -> export -> restore into a clean DB ->
# verify -> byte-identical re-export, plus usage and tamper rejections.
#   cmake -DBUNDLE=<bundle exe> -DEXAMPLE=<examples/native-sqlite> -DWORK=<dir> -P cli_end_to_end.cmake

file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")
set(DB "${WORK}/finance.db")
set(CLEAN "${WORK}/restored.db")
set(TIME "2026-01-04T12:00:00Z")

function(bundle expected_code expected_text)
    execute_process(COMMAND "${BUNDLE}" ${ARGN}
        RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err)
    string(REPLACE ";" " " shown "${ARGN}")
    if(NOT code EQUAL expected_code)
        message(FATAL_ERROR "bundle ${shown}: exit ${code}, expected ${expected_code}\n${out}\n${err}")
    endif()
    if(NOT expected_text STREQUAL "" AND NOT "${out}${err}" MATCHES "${expected_text}")
        message(FATAL_ERROR "bundle ${shown}: output lacks '${expected_text}'\n${out}\n${err}")
    endif()
    message(STATUS "ok (${code}): bundle ${shown}")
    set(LAST_OUTPUT "${out}" PARENT_SCOPE)
endfunction()

# Help and explicit usage errors (nothing is created).
bundle(0 "Usage:" help)
bundle(2 "unknown command" frobnicate --db "${DB}")
bundle(2 "explicit offset" calculate --db "${DB}" --as-of 2026-01-04 --horizon-end 2026-02-28T00:00:00Z)
if(EXISTS "${DB}")
    message(FATAL_ERROR "a usage error created the database")
endif()

# Import (twice: the second admission must change nothing), balances, planning.
bundle(0 "database ready" init --db "${DB}")
bundle(0 "admitted batch csv:" import-csv --db "${DB}" --source-id example-csv-2026-01
       --imported-at "${TIME}" "${EXAMPLE}/statement.csv")
bundle(0 "nothing changed" import-csv --db "${DB}" --source-id example-csv-2026-01
       --imported-at "${TIME}" "${EXAMPLE}/statement.csv")
bundle(0 "admitted batch example-balances:" import-batch --db "${DB}" "${EXAMPLE}/balances.json")
execute_process(COMMAND "${CMAKE_COMMAND}" -DOUT=${WORK}/planning -P "${EXAMPLE}/make-planning-archive.cmake"
    RESULT_VARIABLE packed)
if(NOT packed EQUAL 0)
    message(FATAL_ERROR "make-planning-archive.cmake failed")
endif()
bundle(0 "archive ok" verify-archive "${WORK}/planning")
bundle(0 "restored" restore --db "${DB}" "${WORK}/planning")

# Inspect history and accounts.
bundle(0 "import_batch.admitted" history --db "${DB}")
bundle(0 "transaction_revision.appended" history --db "${DB}" --type transaction_revision.appended --json)
bundle(0 "checking +checking +USD +143125 +141250 +fresh" accounts --db "${DB}")

# Deterministic calculation, recorded once.
bundle(0 "safe_to_spend_minor 42250 USD" calculate --db "${DB}" --as-of "${TIME}"
       --horizon-end 2026-02-28T00:00:00Z --floor-minor 20000)
bundle(0 "already recorded" calculate --db "${DB}" --as-of "${TIME}"
       --horizon-end 2026-02-28T00:00:00Z --floor-minor 20000)

# Export, then restore into a clean database in a fresh process and verify.
bundle(0 "exported" export --db "${DB}" --created-at 2026-01-06T00:00:00Z "${WORK}/archive")
bundle(1 "already exists" export --db "${DB}" --created-at 2026-01-06T00:00:00Z "${WORK}/archive")
bundle(0 "archive ok" verify-archive "${WORK}/archive")
bundle(0 "restored" restore --db "${CLEAN}" "${WORK}/archive")
bundle(0 "restored .* \\(0 new events\\)" restore --db "${CLEAN}" "${WORK}/archive")
bundle(0 "safe_to_spend_minor 42250 USD.*already recorded" calculate --db "${CLEAN}" --as-of "${TIME}"
       --horizon-end 2026-02-28T00:00:00Z --floor-minor 20000)
bundle(0 "exported" export --db "${CLEAN}" --created-at 2026-01-06T00:00:00Z "${WORK}/re-export")
foreach(name manifest.json accounts.json balances.jsonl transactions.jsonl commitments.json budgets.json
        goals.json overrides.jsonl receipts.jsonl)
    file(SHA256 "${WORK}/archive/${name}" original)
    file(SHA256 "${WORK}/re-export/${name}" again)
    if(NOT original STREQUAL again)
        message(FATAL_ERROR "${name} is not byte-stable across export -> restore -> export")
    endif()
endforeach()
message(STATUS "ok: all 9 archive files byte-identical after restore into a clean database")

# Tampering is rejected and the clean database is untouched.
file(COPY "${WORK}/archive/" DESTINATION "${WORK}/tampered")
file(READ "${WORK}/tampered/goals.json" goals)
string(REPLACE "Emergency buffer" "Emergency bluffer" goals "${goals}")
file(WRITE "${WORK}/tampered/goals.json" "${goals}")
file(SHA256 "${CLEAN}" before)
bundle(1 "hash or size mismatch for goals.json" restore --db "${CLEAN}" "${WORK}/tampered")
bundle(1 "hash or size mismatch" verify-archive "${WORK}/tampered")
file(SHA256 "${CLEAN}" after)
if(NOT before STREQUAL after)
    message(FATAL_ERROR "rejected restore modified the database file")
endif()
message(STATUS "ok: tampered archive rejected; database file unchanged")
