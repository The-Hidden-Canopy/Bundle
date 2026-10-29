// Negative control for the test checks (risk register R-012). NOT a CTest test.
//
// Build in a Release (NDEBUG) configuration and run directly. Expected result:
// exit status 1 with "CHECK failed" on stderr. It proves that a failing check
// still fails a test when assert() is compiled out. Exit status 2 means the
// build was not an NDEBUG build, so the control would prove nothing.

#include "bundle_test/check.hpp"

#include <cassert>
#include <iostream>

int main() {
#ifndef NDEBUG
    std::cerr << "negative control: NDEBUG is not defined; configure with CMAKE_BUILD_TYPE=Release\n";
    return 2;
#else
    std::cout << "negative control: NDEBUG is defined (assert() is inert in this build)\n";
    assert(false && "compiled out under NDEBUG; reaching the next line proves it");
    std::cout << "negative control: assert(false) did not stop execution\n";
    CHECK(1 + 1 == 3);  // must fail and exit 1
    std::cerr << "negative control: CHECK did not fail -- the test checks are inert\n";
    return 0;
#endif
}
