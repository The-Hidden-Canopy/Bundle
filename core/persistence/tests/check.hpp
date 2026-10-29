#pragma once

// A check that stays active in Release builds (unlike assert under NDEBUG).

#include <cstdlib>
#include <iostream>
#include <string>

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            std::cerr << __FILE__ << ":" << __LINE__ << ": CHECK failed: " #condition "\n";        \
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
            static_cast<void>(statement);                                                                             \
        } catch (const exception_type&) {                                                          \
            check_threw_ = true;                                                                   \
        }                                                                                          \
        if (!check_threw_) {                                                                       \
            std::cerr << __FILE__ << ":" << __LINE__ << ": expected " #exception_type " from " #statement "\n"; \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (false)
