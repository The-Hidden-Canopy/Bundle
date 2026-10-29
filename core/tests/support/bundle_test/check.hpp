#pragma once

// Test checks that are active in every build type.
//
// assert() is removed when NDEBUG is defined (CMake's Release and
// RelWithDebInfo), which silently turns a Release test run into a no-op --
// including any setup call written inside the assert. These macros never
// depend on NDEBUG: a failed check prints its location and expression and
// exits with status 1, so CTest reports the test as failed in every build.
// CHECK is variadic so expressions containing top-level commas work unchanged.

#include <cstdlib>
#include <iostream>

#define CHECK(...)                                                                                 \
    do {                                                                                           \
        if (!(__VA_ARGS__)) {                                                                      \
            std::cerr << __FILE__ << ":" << __LINE__ << ": CHECK failed: " #__VA_ARGS__ "\n";      \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (false)

#define CHECK_EQ(actual, expected)                                                                 \
    do {                                                                                           \
        const auto check_actual_ = (actual);                                                       \
        const auto check_expected_ = (expected);                                                   \
        if (!(check_actual_ == check_expected_)) {                                                 \
            std::cerr << __FILE__ << ":" << __LINE__ << ": CHECK_EQ failed: " #actual " == " #expected \
                      << "\n  actual:   " << check_actual_ << "\n  expected: " << check_expected_ << "\n"; \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (false)

#define CHECK_THROWS(statement, exception_type)                                                   \
    do {                                                                                           \
        bool check_threw_ = false;                                                                 \
        try {                                                                                      \
            static_cast<void>(statement);                                                          \
        } catch (const exception_type&) {                                                          \
            check_threw_ = true;                                                                   \
        }                                                                                          \
        if (!check_threw_) {                                                                       \
            std::cerr << __FILE__ << ":" << __LINE__ << ": expected " #exception_type " from " #statement "\n"; \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (false)
