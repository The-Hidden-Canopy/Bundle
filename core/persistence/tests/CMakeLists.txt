add_executable(bundle_persistence_format_tests test_formats.cpp)
target_link_libraries(bundle_persistence_format_tests PRIVATE bundle::persistence)
add_test(NAME bundle.persistence.formats COMMAND bundle_persistence_format_tests)

add_executable(bundle_persistence_recovery_tests test_recovery.cpp)
target_link_libraries(bundle_persistence_recovery_tests PRIVATE bundle::persistence SQLite::SQLite3)
target_compile_definitions(bundle_persistence_recovery_tests PRIVATE
    BUNDLE_PARITY_FIXTURES="${PROJECT_SOURCE_DIR}/tests/parity/fixtures")
if(MINGW)
    target_link_options(bundle_persistence_recovery_tests PRIVATE -static)
endif()
add_test(NAME bundle.persistence.recovery COMMAND bundle_persistence_recovery_tests)

# End-to-end through the real CLI: import -> history -> calculate -> export ->
# restart -> restore into a clean database -> verify -> byte-identical re-export.
add_test(NAME bundle.cli.end_to_end
    COMMAND ${CMAKE_COMMAND}
        -DBUNDLE=$<TARGET_FILE:bundle_cli>
        -DEXAMPLE=${PROJECT_SOURCE_DIR}/examples/native-sqlite
        -DWORK=${CMAKE_CURRENT_BINARY_DIR}/e2e
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_end_to_end.cmake)
