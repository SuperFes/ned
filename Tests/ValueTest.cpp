#include <catch2/catch_test_macros.hpp>

#include <janet.h>

#include <stdexcept>
#include <string>

#include "Janet/Value.h"
#include "JanetTestSupport.h"

// Relies on the shared Environment/Janet VM set up once for the whole binary
// by JanetTestSupport.cpp -- see that file for why each test can't manage
// its own janet_init()/janet_deinit() cycle (or construct its own Environment).

using ned::janet::FromJanet;
using ned::janet::RootedValue;
using ned::janet::ToJanet;

TEST_CASE("Bool round-trips through ToJanet/FromJanet", "[Value]") {
    REQUIRE(FromJanet<bool>(ToJanet(true)) == true);
    REQUIRE(FromJanet<bool>(ToJanet(false)) == false);
    REQUIRE_THROWS_AS(FromJanet<bool>(janet_wrap_number(1)), std::runtime_error);
}

TEST_CASE("Numeric types round-trip through ToJanet/FromJanet", "[Value]") {
    REQUIRE(FromJanet<std::int64_t>(ToJanet(std::int64_t{-42})) == -42);
    REQUIRE(FromJanet<std::size_t>(ToJanet(std::size_t{7})) == 7);
    REQUIRE(FromJanet<double>(ToJanet(3.5)) == 3.5);

    REQUIRE_THROWS_AS(FromJanet<std::int64_t>(janet_wrap_string(janet_cstring("x"))), std::runtime_error);
}

TEST_CASE("FromJanet<size_t> rejects negative numbers", "[Value]") {
    REQUIRE_THROWS_AS(FromJanet<std::size_t>(janet_wrap_number(-1)), std::runtime_error);
}

TEST_CASE("String round-trips through ToJanet/FromJanet", "[Value]") {
    REQUIRE(FromJanet<std::string>(ToJanet(std::string("hello"))) == "hello");
    REQUIRE_THROWS_AS(FromJanet<std::string>(janet_wrap_number(1)), std::runtime_error);
}

TEST_CASE("RootedValue accepts a function and rejects non-functions", "[Value]") {
    JanetTable* env = ned_tests::TestEnvironment().Env();
    Janet       out;
    REQUIRE(janet_dostring(env, "(fn [] 1)", "test", &out) == 0);

    const RootedValue rooted = FromJanet<RootedValue>(out);
    REQUIRE(janet_checktype(rooted.Get(), JANET_FUNCTION));

    REQUIRE_THROWS_AS(FromJanet<RootedValue>(janet_wrap_number(1)), std::runtime_error);
}

TEST_CASE("RootedValue keeps its function alive across a GC cycle", "[Value]") {
    // Note: janet_collect() requires an active fiber context (calling it at
    // bare top level segfaults in this Janet version) -- a real constraint of
    // the C API, not specific to our wrapper. janet_dostring establishes that
    // context properly, so GC pressure + a real call is how a collection is
    // actually forced here.
    JanetTable* env = ned_tests::TestEnvironment().Env();
    Janet       out;
    REQUIRE(janet_dostring(env, "(fn [] 42)", "test", &out) == 0);

    RootedValue rooted = FromJanet<RootedValue>(out);

    JanetFunction*      fn          = janet_unwrap_function(rooted.Get());
    const JanetFuncDef* originalDef = fn->def;
    REQUIRE(originalDef != nullptr);

    // Drop every other reference to the function value, force GC pressure,
    // and allocate garbage inside a real call so a collection actually runs.
    // If RootedValue didn't actually gcroot it, this backing memory could be
    // reclaimed, and dereferencing it below would read freed/corrupted data.
    out = janet_wrap_nil();
    janet_gcpressure(1 << 20);
    Janet churn;
    REQUIRE(janet_dostring(env, "(seq [i :range [0 200]] (string i))", "churn", &churn) == 0);

    REQUIRE(janet_checktype(rooted.Get(), JANET_FUNCTION));

    JanetFunction* stillFn = janet_unwrap_function(rooted.Get());
    REQUIRE(stillFn == fn); // same object, not reallocated elsewhere
    REQUIRE(stillFn->def == originalDef);
    REQUIRE(stillFn->def->bytecode != nullptr);
}
