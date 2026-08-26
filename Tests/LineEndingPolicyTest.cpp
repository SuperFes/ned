#include <catch2/catch_test_macros.hpp>

#include "Editor/LineEndingPolicy.h"

using namespace ned::editor;
using ned::text::LineEnding;

namespace {

// Process-wide state (LineEndingPolicy.h's own doc comment) -- every test
// that sets it must restore the default for the next test, guaranteed via
// RAII. Mirrors FinalNewlineTest.cpp's own FinalNewlineGuard exactly.
struct LineEndingPolicyGuard {
    ~LineEndingPolicyGuard() { SetLineEndingPolicy({LineEndingPolicyMode::Preserve, LineEnding::LF}); }
};

} // namespace

TEST_CASE("LineEndingPolicy defaults to Preserve", "[LineEndingPolicy]") {
    const LineEndingPolicyGuard guard;
    const LineEndingPolicy      policy = GetLineEndingPolicy();
    REQUIRE(policy.mode == LineEndingPolicyMode::Preserve);
}

TEST_CASE("ResolveLineEndingForSave passes the buffer's own ending through under Preserve", "[LineEndingPolicy]") {
    const LineEndingPolicyGuard guard;
    CHECK(ResolveLineEndingForSave(LineEnding::LF) == LineEnding::LF);
    CHECK(ResolveLineEndingForSave(LineEnding::CRLF) == LineEnding::CRLF);
    CHECK(ResolveLineEndingForSave(LineEnding::CR) == LineEnding::CR);
}

TEST_CASE("ResolveLineEndingForSave overrides to the forced ending under Force", "[LineEndingPolicy]") {
    const LineEndingPolicyGuard guard;
    SetLineEndingPolicy({LineEndingPolicyMode::Force, LineEnding::LF});
    CHECK(ResolveLineEndingForSave(LineEnding::CRLF) == LineEnding::LF);
    CHECK(ResolveLineEndingForSave(LineEnding::LF) == LineEnding::LF);

    SetLineEndingPolicy({LineEndingPolicyMode::Force, LineEnding::CRLF});
    CHECK(ResolveLineEndingForSave(LineEnding::LF) == LineEnding::CRLF);
}

TEST_CASE("SetLineEndingPolicyFromString parses every recognized token", "[LineEndingPolicy]") {
    const LineEndingPolicyGuard guard;

    SetLineEndingPolicyFromString("lf");
    CHECK(GetLineEndingPolicy().mode == LineEndingPolicyMode::Force);
    CHECK(GetLineEndingPolicy().forcedEnding == LineEnding::LF);

    SetLineEndingPolicyFromString("crlf");
    CHECK(GetLineEndingPolicy().mode == LineEndingPolicyMode::Force);
    CHECK(GetLineEndingPolicy().forcedEnding == LineEnding::CRLF);

    SetLineEndingPolicyFromString("cr");
    CHECK(GetLineEndingPolicy().mode == LineEndingPolicyMode::Force);
    CHECK(GetLineEndingPolicy().forcedEnding == LineEnding::CR);

    SetLineEndingPolicyFromString("preserve");
    CHECK(GetLineEndingPolicy().mode == LineEndingPolicyMode::Preserve);
}

TEST_CASE("SetLineEndingPolicyFromString ignores an unrecognized token", "[LineEndingPolicy]") {
    const LineEndingPolicyGuard guard;
    SetLineEndingPolicyFromString("crlf");
    SetLineEndingPolicyFromString("nonsense");
    CHECK(GetLineEndingPolicy().mode == LineEndingPolicyMode::Force);
    CHECK(GetLineEndingPolicy().forcedEnding == LineEnding::CRLF);
}
