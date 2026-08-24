#include <catch2/catch_test_macros.hpp>

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
