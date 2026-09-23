//
// The pure half of change-signature (Editor/ChangeSignature.h): matching an
// old and a new parameter list by name into a position mapping, and
// rewriting one call site's own argument list against that mapping. No
// Buffer, no Parser, no Mode -- every SignatureParameter/CallArgument here
// is hand-built, which is the point of the module being pure.
//

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>
#include <vector>

#include "Editor/ChangeSignature.h"

using ned::editor::CallArgument;
using ned::editor::SignatureParameter;
using ned::editor::changesig::ArgumentRewrite;
using ned::editor::changesig::BuildPositionMapping;
using ned::editor::changesig::MappingResult;
using ned::editor::changesig::ParamOrigin;
using ned::editor::changesig::ParamOriginKind;
using ned::editor::changesig::RewriteArgumentList;

namespace {

SignatureParameter Param(std::string_view text, std::string_view name) {
    const std::size_t start = text.find(name);
    REQUIRE(start != std::string_view::npos);
    SignatureParameter parameter{};
    parameter.startByte     = start;
    parameter.endByte       = start + name.size();
    parameter.nameStartByte = start;
    parameter.nameEndByte   = start + name.size();
    return parameter;
}

SignatureParameter ParamWithDefault(std::string_view text, std::string_view name, std::string_view defaultExpr) {
    SignatureParameter parameter = Param(text, name);
    const std::size_t  defStart  = text.find(defaultExpr, parameter.nameEndByte);
    REQUIRE(defStart != std::string_view::npos);
    parameter.hasDefaultValue  = true;
    parameter.defaultStartByte = defStart;
    parameter.defaultEndByte   = defStart + defaultExpr.size();
    return parameter;
}

SignatureParameter VariadicParam(std::string_view text, std::string_view token) {
    const std::size_t start = text.find(token);
    REQUIRE(start != std::string_view::npos);
    SignatureParameter parameter{};
    parameter.startByte  = start;
    parameter.endByte    = start + token.size();
    parameter.isVariadic = true;
    return parameter;
}

CallArgument Arg(std::string_view text, std::string_view value) {
    const std::size_t start = text.find(value);
    REQUIRE(start != std::string_view::npos);
    return CallArgument{.startByte = start, .endByte = start + value.size()};
}

} // namespace

TEST_CASE("BuildPositionMapping keeps a call site's own values across a pure reorder", "[ChangeSignature]") {
    const std::string oldText = "int a, int b";
    const std::string newText = "int b, int a";
    const std::vector<SignatureParameter> oldParams{Param(oldText, "a"), Param(oldText, "b")};
    const std::vector<SignatureParameter> newParams{Param(newText, "b"), Param(newText, "a")};

    const MappingResult mapping = BuildPositionMapping(oldText, oldParams, newText, newParams);
    REQUIRE_FALSE(mapping.declined);
    REQUIRE(mapping.origins.size() == 2);
    CHECK(mapping.origins[0].kind == ParamOriginKind::Kept);
    CHECK(mapping.origins[0].oldIndex == 1); // new[0] is "b", old index 1
    CHECK(mapping.origins[1].kind == ParamOriginKind::Kept);
    CHECK(mapping.origins[1].oldIndex == 0); // new[1] is "a", old index 0

    const std::string callText = "f(10, 20)";
    const std::vector<CallArgument> callArgs{Arg(callText, "10"), Arg(callText, "20")};
    const ArgumentRewrite rewrite = RewriteArgumentList(callText, callArgs, newText, mapping.origins);
    REQUIRE_FALSE(rewrite.declined);
    CHECK(rewrite.argumentListText == "20, 10");
}

TEST_CASE("BuildPositionMapping appends a new defaulted parameter's default at every call site", "[ChangeSignature]") {
    const std::string oldText = "int a";
    const std::string newText = "int a, bool flag = true";
    const std::vector<SignatureParameter> oldParams{Param(oldText, "a")};
    const std::vector<SignatureParameter> newParams{Param(newText, "a"), ParamWithDefault(newText, "flag", "true")};

    const MappingResult mapping = BuildPositionMapping(oldText, oldParams, newText, newParams);
    REQUIRE_FALSE(mapping.declined);
    REQUIRE(mapping.origins.size() == 2);
    CHECK(mapping.origins[0].kind == ParamOriginKind::Kept);
    CHECK(mapping.origins[1].kind == ParamOriginKind::New);

    const std::string callText = "f(5)";
    const std::vector<CallArgument> callArgs{Arg(callText, "5")};
    const ArgumentRewrite rewrite = RewriteArgumentList(callText, callArgs, newText, mapping.origins);
    REQUIRE_FALSE(rewrite.declined);
    CHECK(rewrite.argumentListText == "5, true");
}

TEST_CASE("BuildPositionMapping drops a removed parameter's call-site argument", "[ChangeSignature]") {
    const std::string oldText = "int a, int b";
    const std::string newText = "int a";
    const std::vector<SignatureParameter> oldParams{Param(oldText, "a"), Param(oldText, "b")};
    const std::vector<SignatureParameter> newParams{Param(newText, "a")};

    const MappingResult mapping = BuildPositionMapping(oldText, oldParams, newText, newParams);
    REQUIRE_FALSE(mapping.declined);
    REQUIRE(mapping.origins.size() == 1);

    const std::string callText = "f(1, 2)";
    const std::vector<CallArgument> callArgs{Arg(callText, "1"), Arg(callText, "2")};
    const ArgumentRewrite rewrite = RewriteArgumentList(callText, callArgs, newText, mapping.origins);
    REQUIRE_FALSE(rewrite.declined);
    CHECK(rewrite.argumentListText == "1");
}

TEST_CASE("BuildPositionMapping declines a new parameter with no default", "[ChangeSignature]") {
    const std::string oldText = "int a";
    const std::string newText = "int a, bool flag";
    const std::vector<SignatureParameter> oldParams{Param(oldText, "a")};
    const std::vector<SignatureParameter> newParams{Param(newText, "a"), Param(newText, "flag")};

    const MappingResult mapping = BuildPositionMapping(oldText, oldParams, newText, newParams);
    REQUIRE(mapping.declined);
    CHECK(mapping.declineReason.find("flag") != std::string::npos);
    CHECK(mapping.declineReason.find("default") != std::string::npos);
}

TEST_CASE("BuildPositionMapping declines a new parameter list with a repeated name", "[ChangeSignature]") {
    const std::string oldText = "int a";
    const std::string newText = "int a, int a";
    const std::vector<SignatureParameter> oldParams{Param(oldText, "a")};
    // Both new entries are named "a" -- point the second at the second
    // occurrence so the two ranges don't collide.
    SignatureParameter second = Param(newText, "a");
    second.nameStartByte      = newText.rfind("a");
    second.nameEndByte        = second.nameStartByte + 1;
    second.startByte          = second.nameStartByte;
    second.endByte            = second.nameEndByte;
    const std::vector<SignatureParameter> newParams{Param(newText, "a"), second};

    const MappingResult mapping = BuildPositionMapping(oldText, oldParams, newText, newParams);
    REQUIRE(mapping.declined);
    CHECK(mapping.declineReason.find("more than once") != std::string::npos);
}

TEST_CASE("BuildPositionMapping declines outright when either side has a variadic parameter", "[ChangeSignature]") {
    const std::string oldText = "int a, ...";
    const std::string newText = "int a";
    const std::vector<SignatureParameter> oldParams{Param(oldText, "a"), VariadicParam(oldText, "...")};
    const std::vector<SignatureParameter> newParams{Param(newText, "a")};

    const MappingResult mapping = BuildPositionMapping(oldText, oldParams, newText, newParams);
    REQUIRE(mapping.declined);
    CHECK(mapping.declineReason.find("variadic") != std::string::npos);
}

TEST_CASE("BuildPositionMapping treats a duplicated OLD name as unmatchable, not a guess", "[ChangeSignature]") {
    // Two old parameters both spelled "x" (malformed input, but this module
    // never assumes which one a same-named new parameter meant).
    const std::string oldText = "int x, int x, int y";
    SignatureParameter firstX  = Param(oldText, "x");
    SignatureParameter secondX = firstX;
    secondX.nameStartByte      = oldText.find("x,", firstX.nameEndByte);
    secondX.nameEndByte        = secondX.nameStartByte + 1;
    secondX.startByte          = secondX.nameStartByte;
    secondX.endByte            = secondX.nameEndByte;
    const std::vector<SignatureParameter> oldParams{firstX, secondX, Param(oldText, "y")};

    const std::string newText = "int x, int y";
    const std::vector<SignatureParameter> newParams{Param(newText, "x"), Param(newText, "y")};

    const MappingResult mapping = BuildPositionMapping(oldText, oldParams, newText, newParams);
    // "x" can't be matched unambiguously (declines with no default), "y"
    // matches cleanly.
    REQUIRE(mapping.declined);
    CHECK(mapping.declineReason.find("x") != std::string::npos);
}

TEST_CASE("RewriteArgumentList declines a call site that supplies fewer arguments than the old signature",
          "[ChangeSignature]") {
    const std::string oldText = "int a, int b";
    const std::string newText = "int b, int a";
    const std::vector<SignatureParameter> oldParams{Param(oldText, "a"), Param(oldText, "b")};
    const std::vector<SignatureParameter> newParams{Param(newText, "b"), Param(newText, "a")};
    const MappingResult mapping = BuildPositionMapping(oldText, oldParams, newText, newParams);
    REQUIRE_FALSE(mapping.declined);

    // The call site relies on b's own default and supplies only one
    // argument -- this module has no text for the omitted default.
    const std::string callText = "f(1)";
    const std::vector<CallArgument> callArgs{Arg(callText, "1")};
    const ArgumentRewrite rewrite = RewriteArgumentList(callText, callArgs, newText, mapping.origins);
    REQUIRE(rewrite.declined);
    CHECK(rewrite.declineReason.find("fewer arguments") != std::string::npos);
}
