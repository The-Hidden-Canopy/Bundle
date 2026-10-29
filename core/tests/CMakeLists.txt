# Test checks that stay active under NDEBUG (see support/bundle_test/check.hpp).
add_library(bundle_test_support INTERFACE)
target_include_directories(bundle_test_support INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/support)

add_executable(bundle_core_activity_tests
    unit/test_activity.cpp
)

target_link_libraries(bundle_core_activity_tests PRIVATE bundle::core)

add_test(NAME bundle.core.activity COMMAND bundle_core_activity_tests)

add_executable(bundle_core_financial_tests
    unit/test_financial_types.cpp
)

target_link_libraries(bundle_core_financial_tests PRIVATE bundle::core)

add_test(NAME bundle.core.financial_types COMMAND bundle_core_financial_tests)

add_executable(bundle_core_planning_tests
    unit/test_planning.cpp
)

target_link_libraries(bundle_core_planning_tests PRIVATE bundle::core)

add_test(NAME bundle.core.planning COMMAND bundle_core_planning_tests)

add_executable(bundle_core_store_tests
    unit/test_store.cpp
)

target_link_libraries(bundle_core_store_tests PRIVATE bundle::core)

add_test(NAME bundle.core.store COMMAND bundle_core_store_tests)

add_executable(bundle_core_commitment_tests
    unit/test_commitments.cpp
)

target_link_libraries(bundle_core_commitment_tests PRIVATE bundle::core)

add_test(NAME bundle.core.commitments COMMAND bundle_core_commitment_tests)

add_executable(bundle_core_budget_tests
    unit/test_budgets.cpp
)

target_link_libraries(bundle_core_budget_tests PRIVATE bundle::core)

add_test(NAME bundle.core.budgets COMMAND bundle_core_budget_tests)

add_executable(bundle_core_reconciliation_tests
    unit/test_reconciliation.cpp
)

target_link_libraries(bundle_core_reconciliation_tests PRIVATE bundle::core)

add_test(NAME bundle.core.reconciliation COMMAND bundle_core_reconciliation_tests)

foreach(test_target
        bundle_core_activity_tests bundle_core_financial_tests bundle_core_planning_tests
        bundle_core_store_tests bundle_core_commitment_tests bundle_core_budget_tests
        bundle_core_reconciliation_tests)
    target_link_libraries(${test_target} PRIVATE bundle_test_support)
endforeach()

# Negative control: built with the tests but deliberately NOT registered with
# CTest, so the normal suite stays all-passing. Run it directly in a Release
# build; it must exit 1 ("CHECK failed"). See core/tests/negative_control/.
add_executable(bundle_check_negative_control negative_control/check_negative_control.cpp)
target_link_libraries(bundle_check_negative_control PRIVATE bundle_test_support)
