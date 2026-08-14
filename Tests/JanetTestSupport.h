#ifndef NED_TESTS_JANET_TEST_SUPPORT_H
#define NED_TESTS_JANET_TEST_SUPPORT_H

#include "Janet/Environment.h"

namespace ned_tests {

// The single Environment (and therefore the single Janet VM) shared by every
// Janet-related test in the binary. See JanetTestSupport.cpp for why: each
// Environment owns a janet_init()/janet_deinit() pair, and constructing more
// than one -- even sequentially -- corrupts state in this Janet build.
// Valid only during a test run (constructed in testRunStarting).
[[nodiscard]] ned::janet::Environment& TestEnvironment();

} // namespace ned_tests

#endif // NED_TESTS_JANET_TEST_SUPPORT_H
