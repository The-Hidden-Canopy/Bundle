add_library(bundle_core STATIC
    src/activity.cpp
    src/budgets.cpp
    src/commitments.cpp
    src/financial.cpp
    src/money.cpp
    src/planning.cpp
    src/reconciliation.cpp
    src/store.cpp
)

add_library(bundle::core ALIAS bundle_core)

target_compile_features(bundle_core PUBLIC cxx_std_20)

target_include_directories(bundle_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

if(MSVC)
    target_compile_options(bundle_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(bundle_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUNDLE_BUILD_TESTS)
    add_subdirectory(tests)
endif()
