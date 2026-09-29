cmake_minimum_required(VERSION 3.20)

project(bundle VERSION 0.1.0 LANGUAGES CXX)

option(BUNDLE_BUILD_TESTS "Build the BUNDLE unit tests" ON)

if(BUNDLE_BUILD_TESTS)
    include(CTest)
    enable_testing()
endif()

add_subdirectory(core)
