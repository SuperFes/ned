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
    const ArgumentRewrite           rewrite = RewriteArgumentList(callText, callArgs, newText, mapping);
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
    const ArgumentRewrite           rewrite = RewriteArgumentList(callText, callArgs, newText, mapping);
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
    const ArgumentRewrite           rewrite = RewriteArgumentList(callText, callArgs, newText, mapping);
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
    const ArgumentRewrite           rewrite = RewriteArgumentList(callText, callArgs, newText, mapping);
    REQUIRE(rewrite.declined);
    CHECK(rewrite.declineReason.find("fewer arguments") != std::string::npos);
}

namespace {

// A parameter with no name capture -- a pattern such as `0` or `(x:xs)`.
SignatureParameter Pattern(std::string_view text, std::string_view pattern, std::size_t from = 0) {
    const std::size_t start = text.find(pattern, from);
    REQUIRE(start != std::string_view::npos);
    SignatureParameter parameter{};
    parameter.startByte = start;
    parameter.endByte   = start + pattern.size();
    return parameter;
}

MappingResult Reorder(std::string_view oldText, std::vector<SignatureParameter> oldParams, std::string_view newText,
                      std::vector<SignatureParameter> newParams) {
    MappingResult mapping = BuildPositionMapping(oldText, oldParams, newText, newParams);
    REQUIRE_FALSE(mapping.declined);
    return mapping;
}

std::string Applied(const std::string& text, const ned::editor::changesig::SiteRewrite& rewrite) {
    return text.substr(0, rewrite.startByte) + rewrite.text + text.substr(rewrite.endByte);
}

} // namespace

TEST_CASE("BuildPositionMapping matches a nameless pattern parameter by its text", "[ChangeSignature]") {
    const std::string   oldText = "0 (x:xs)";
    const std::string   newText = "(x:xs) 0";
    const MappingResult mapping = Reorder(oldText, {Pattern(oldText, "0"), Pattern(oldText, "(x:xs)")}, newText,
                                          {Pattern(newText, "(x:xs)"), Pattern(newText, "0")});
    REQUIRE(mapping.origins.size() == 2);
    CHECK(mapping.origins[0].oldIndex == 1);
    CHECK(mapping.origins[1].oldIndex == 0);
}

TEST_CASE("RewriteArgumentList declines a curried call that applies the function partially", "[ChangeSignature]") {
    const std::string   oldText = "a b";
    const std::string   newText = "a";
    const MappingResult mapping = Reorder(oldText, {Param(oldText, "a"), Param(oldText, "b")}, newText, {Param(newText, "a")});

    const std::string               callText = "f 1";
    const std::vector<CallArgument> callArgs{Arg(callText, "1")};
    CHECK(RewriteArgumentList(callText, callArgs, newText, mapping).argumentListText == "1");
    const ArgumentRewrite curried =
        RewriteArgumentList(callText, callArgs, newText, mapping, ned::editor::CallReceiver::None, " ", true);
    REQUIRE(curried.declined);
    CHECK(curried.declineReason == "call site applies the function partially");
}

TEST_CASE("PermuteSignature reorders a type's components and keeps each separator where it was", "[ChangeSignature]") {
    using ned::editor::SignatureKind;
    using ned::editor::SignatureMarker;
    using ned::editor::changesig::PermuteSignature;
    const std::string oldText = "x n";
    const std::string swapped = "n x";
    const std::string dropped = "x";

    const std::string type = "f :: Float\n  -> Int\n  -> Float\n";
    SignatureMarker   curried{};
    curried.kind       = SignatureKind::CurriedType;
    curried.parameters = {Pattern(type, "Float"), Pattern(type, "Int"), Pattern(type, "Float", type.find("Int"))};

    const MappingResult swap = Reorder(oldText, {Param(oldText, "x"), Param(oldText, "n")}, swapped,
                                       {Param(swapped, "n"), Param(swapped, "x")});
    CHECK(Applied(type, PermuteSignature(type, curried, swap)) == "f :: Int\n  -> Float\n  -> Float\n");
    const MappingResult drop = Reorder(oldText, {Param(oldText, "x"), Param(oldText, "n")}, dropped, {Param(dropped, "x")});
    CHECK(Applied(type, PermuteSignature(type, curried, drop)) == "f :: Float\n  -> Float\n");

    // A synonym spells no parameter out.
    SignatureMarker opaque = curried;
    opaque.parameters      = {Pattern(type, "Float")};
    CHECK(PermuteSignature(type, opaque, swap).declined);

    // Another clause's patterns: dropping them all takes the space before
    // the list with them.
    const std::string clause = "f 0 _ = 0\n";
    SignatureMarker   definition{};
    definition.parameters                  = {Pattern(clause, "0"), Pattern(clause, "_")};
    definition.parametersInteriorStartByte = 1;
    definition.parametersInteriorEndByte   = 5;
    definition.parametersLead              = true;
    CHECK(Applied(clause, PermuteSignature(clause, definition, swap)) == "f _ 0 = 0\n");
    const std::string   none    = "";
    const MappingResult dropAll = Reorder(oldText, {Param(oldText, "x"), Param(oldText, "n")}, none, {});
    CHECK(Applied(clause, PermuteSignature(clause, definition, dropAll)) == "f = 0\n");

    // Nothing says what another clause matches a new parameter with.
    const std::string  added = "x n (k = 1)";
    SignatureParameter k     = Param(added, "k");
    k.hasDefaultValue        = true;
    k.defaultStartByte       = added.find('1');
    k.defaultEndByte         = k.defaultStartByte + 1;
    const MappingResult grow = Reorder(oldText, {Param(oldText, "x"), Param(oldText, "n")}, added,
                                       {Param(added, "x"), Param(added, "n"), k});
    CHECK(PermuteSignature(clause, definition, grow).declined);
}

TEST_CASE("RewriteArityReference follows the arity it names", "[ChangeSignature]") {
    using ned::editor::CallMarker;
    const std::string   oldText = "A, B";
    const std::string   newText = "A";
    const MappingResult mapping = Reorder(oldText, {Param(oldText, "A"), Param(oldText, "B")}, newText, {Param(newText, "A")});

    const std::string text = "fun f/2, fun f/1";
    CallMarker        full{};
    full.arityStartByte = text.find('2');
    full.arityEndByte   = full.arityStartByte + 1;
    CHECK(ned::editor::changesig::RewriteArityReference(text, full, mapping).argumentListText == "1");
    CallMarker shorter{};
    shorter.arityStartByte = text.rfind('1');
    shorter.arityEndByte   = shorter.arityStartByte + 1;
    CHECK(ned::editor::changesig::RewriteArityReference(text, shorter, mapping).declined);

    // Passed on as a value, it is called elsewhere in the old order -- fine
    // only while that order stands.
    CallMarker value    = full;
    value.functionValue = true;
    CHECK(ned::editor::changesig::RewriteArityReference(text, value, mapping).declined);
    const std::string   same      = "A, B";
    const MappingResult unchanged = Reorder(oldText, {Param(oldText, "A"), Param(oldText, "B")}, same,
                                            {Param(same, "A"), Param(same, "B")});
    CHECK(ned::editor::changesig::RewriteArityReference(text, value, unchanged).argumentListText == "2");
}
