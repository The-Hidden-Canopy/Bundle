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
