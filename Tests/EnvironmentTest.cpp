#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

#include "Janet/Environment.h"
#include "Janet/Value.h"
#include "JanetTestSupport.h"

// Uses the shared Environment (see JanetTestSupport.h) rather than
// constructing its own -- Environment owns a janet_init()/janet_deinit()
// pair, and constructing more than one in this process corrupts state.

using ned::janet::Environment;
using ned::janet::FromJanet;

namespace {
double AddOne(double x) {
    return x + 1;
}
bool IsPositive(double x) {
    return x > 0;
}
} // namespace

TEST_CASE("Environment evaluates simple Janet code and returns the result", "[Environment]") {
    Environment& env = ned_tests::TestEnvironment();
    REQUIRE(FromJanet<double>(env.DoString("(+ 1 2)")) == 3);
}

TEST_CASE("Environment DoString throws on a Janet-level error", "[Environment]") {
    Environment& env = ned_tests::TestEnvironment();
    REQUIRE_THROWS_AS(env.DoString("(this-is-not-defined)"), std::runtime_error);
}

TEST_CASE("Environment DoString captures Janet's real error message, not a generic placeholder", "[Environment]") {
    Environment& env = ned_tests::TestEnvironment();
    REQUIRE_THROWS_WITH(env.DoString("(this-is-not-defined)"), Catch::Matchers::ContainsSubstring("this-is-not-defined"));
}

TEST_CASE("Environment DoString captures a real path:line location, not just the bare message",
          "[Environment]") {
    // raw-stderr-fd-redirect follow-up: DoStringCapturingStacktrace now
    // redirects janet_dostring's own raw stderr stacktrace print into a pipe
    // and prefers that captured text over *out's location-stripped message
    // -- verifies the location (absent from *out alone, per the test above's
    // sibling coverage of the message itself) actually comes through.
    Environment& env = ned_tests::TestEnvironment();
    REQUIRE_THROWS_WITH(env.DoString("\n(this-is-not-defined)", "envtest-location.janet"),
                         Catch::Matchers::ContainsSubstring("envtest-location.janet") &&
                             Catch::Matchers::ContainsSubstring("2"));
}

TEST_CASE("Register wires a free function into the environment", "[Environment]") {
    Environment& env = ned_tests::TestEnvironment();
    env.Register<&AddOne>("envtest", "add-one", "adds one");

    REQUIRE(FromJanet<double>(env.DoString("(envtest/add-one 41)")) == 42);
}

TEST_CASE("Register handles non-numeric return types", "[Environment]") {
    Environment& env = ned_tests::TestEnvironment();
    env.Register<&IsPositive>("envtest", "positive?", "");

    REQUIRE(FromJanet<bool>(env.DoString("(envtest/positive? 5)")) == true);
    REQUIRE(FromJanet<bool>(env.DoString("(envtest/positive? -5)")) == false);
}

TEST_CASE("Register-wrapped function reports a wrong-arity call as a Janet error", "[Environment]") {
    Environment& env = ned_tests::TestEnvironment();
    env.Register<&AddOne>("envtest", "add-one-arity", "");

    REQUIRE_THROWS_AS(env.DoString("(envtest/add-one-arity 1 2)"), std::runtime_error);
}

TEST_CASE("Register-wrapped function converts a type-mismatch throw into a Janet error", "[Environment]") {
    Environment& env = ned_tests::TestEnvironment();
    env.Register<&AddOne>("envtest", "add-one-type", "");

    REQUIRE_THROWS_AS(env.DoString("(envtest/add-one-type \"not a number\")"), std::runtime_error);
}

TEST_CASE("BindingNamesWithPrefix finds a Register-wrapped function by its full prefixed name", "[Environment]") {
    Environment& env = ned_tests::TestEnvironment();
    env.Register<&AddOne>("envtest", "add-one-prefix-scan", "");

    const std::vector<std::string> names = env.BindingNamesWithPrefix("envtest/add-one-prefix");
    REQUIRE(std::find(names.begin(), names.end(), "envtest/add-one-prefix-scan") != names.end());
}

TEST_CASE("BindingNamesWithPrefix excludes symbols not sharing the prefix", "[Environment]") {
    Environment& env = ned_tests::TestEnvironment();
    env.Register<&AddOne>("envtest", "unrelated-symbol", "");

    const std::vector<std::string> names = env.BindingNamesWithPrefix("envtest/add-one-prefix");
    REQUIRE(std::find(names.begin(), names.end(), "envtest/unrelated-symbol") == names.end());
}

TEST_CASE("BindingNamesWithPrefix returns names sorted", "[Environment]") {
    Environment& env = ned_tests::TestEnvironment();
    env.Register<&AddOne>("envtest", "sort-check-b", "");
    env.Register<&AddOne>("envtest", "sort-check-a", "");

    const std::vector<std::string> names = env.BindingNamesWithPrefix("envtest/sort-check-");
    REQUIRE(names.size() == 2);
    CHECK(names[0] == "envtest/sort-check-a");
    CHECK(names[1] == "envtest/sort-check-b");
}
