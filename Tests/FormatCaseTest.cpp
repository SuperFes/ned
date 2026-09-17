#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>
#include <vector>

#include "Editor/FormatCase.h"
#include "Editor/FormatRules.h"
#include "Editor/Mode.h"

using ned::editor::CaseConvention;
using ned::editor::CaseViolation;
using ned::editor::ComputeCaseViolations;
using ned::editor::CppMode;
using ned::editor::Mode;
using ned::editor::SetCaseConvention;
using ned::editor::SuggestNameForConvention;

namespace {

// case-kind follow-up: the same test-isolation lesson FormatWrapTest.cpp's
// own FormatRulesGuard already learned once -- a manual reset at the end of
// a test body never runs if an earlier REQUIRE in that body fails first.
struct FormatRulesGuard {
    ~FormatRulesGuard() {
        SetCaseConvention("parameter", std::nullopt);
        SetCaseConvention("local", std::nullopt);
        SetCaseConvention("function", std::nullopt);
        SetCaseConvention("type", std::nullopt);
        SetCaseConvention("class", std::nullopt);
        SetCaseConvention("struct", std::nullopt);
        SetCaseConvention("enum", std::nullopt);
        SetCaseConvention("enum_member", std::nullopt);
        SetCaseConvention("field", std::nullopt);
        SetCaseConvention("macro", std::nullopt);
        SetCaseConvention("method", std::nullopt);
        SetCaseConvention("template_parameter", std::nullopt);
        SetCaseConvention("namespace", std::nullopt);
        SetCaseConvention("cpp/function", std::nullopt);
    }
};

const CaseViolation* FindViolation(const std::vector<CaseViolation>& violations, std::string_view name) {
    for (const CaseViolation& v : violations) {
        if (v.name == name) {
            return &v;
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE("ComputeCaseViolations reports nothing when no entity kind has a convention configured",
          "[FormatCase]") {
    const Mode        mode   = CppMode();
    const std::string source = "void Bad_Name(int Also_Bad) {\n    int Local_Var = Also_Bad;\n}\n";
    REQUIRE(ComputeCaseViolations(source, "cpp", mode).empty());
}

TEST_CASE("End to end: a badly-named parameter is reported against snake_case, a "
          "conforming one is not",
          "[FormatCase]") {
    const FormatRulesGuard guard;
    SetCaseConvention("parameter", CaseConvention::SnakeCase);

    const Mode        mode   = CppMode();
    const std::string source = "void f(int good_arg, int BadArg) {\n}\n";
    const auto        violations = ComputeCaseViolations(source, "cpp", mode);

    REQUIRE_FALSE(FindViolation(violations, "good_arg"));
    const CaseViolation* bad = FindViolation(violations, "BadArg");
    REQUIRE(bad);
    REQUIRE(bad->entityKind == "parameter");
    REQUIRE(bad->expectedConvention == CaseConvention::SnakeCase);
    REQUIRE(bad->suggestedName == "bad_arg");
}

TEST_CASE("End to end: a badly-named local variable is reported, a parameter with the "
          "same convention untouched when only 'local' is configured",
          "[FormatCase]") {
    const FormatRulesGuard guard;
    SetCaseConvention("local", CaseConvention::SnakeCase);

    const Mode        mode   = CppMode();
    const std::string source = "void f(int BadArg) {\n    int BadLocal = BadArg;\n}\n";
    const auto        violations = ComputeCaseViolations(source, "cpp", mode);

    REQUIRE_FALSE(FindViolation(violations, "BadArg")); // "parameter" kind unconfigured -- untouched
    const CaseViolation* bad = FindViolation(violations, "BadLocal");
    REQUIRE(bad);
    REQUIRE(bad->entityKind == "local");
    REQUIRE(bad->suggestedName == "bad_local");
}

TEST_CASE("End to end: a badly-named function is reported against camelCase", "[FormatCase]") {
    const FormatRulesGuard guard;
    SetCaseConvention("function", CaseConvention::CamelCase);

    const Mode        mode   = CppMode();
    const std::string source = "void Do_The_Thing() {\n}\n";
    const auto        violations = ComputeCaseViolations(source, "cpp", mode);

    const CaseViolation* bad = FindViolation(violations, "Do_The_Thing");
    REQUIRE(bad);
    REQUIRE(bad->entityKind == "function");
    REQUIRE(bad->suggestedName == "doTheThing");
}

TEST_CASE("End to end: a badly-named class and namespace are both reported against "
          "PascalCase and snake_case respectively",
          "[FormatCase]") {
    const FormatRulesGuard guard;
    // case-catalogue follow-up: "class", not "type" -- cpp's own tags.janet
    // now splits class/struct/enum into their own entity kinds instead of
    // conflating them all as "type" (see FormatCase.h's own header comment).
    SetCaseConvention("class", CaseConvention::PascalCase);
    SetCaseConvention("namespace", CaseConvention::SnakeCase);

    const Mode        mode   = CppMode();
    const std::string source = "namespace BadNamespace {\nclass bad_class {\n};\n}\n";
    const auto        violations = ComputeCaseViolations(source, "cpp", mode);

    const CaseViolation* badNs = FindViolation(violations, "BadNamespace");
    REQUIRE(badNs);
    REQUIRE(badNs->entityKind == "namespace");
    REQUIRE(badNs->suggestedName == "bad_namespace");

    const CaseViolation* badClass = FindViolation(violations, "bad_class");
    REQUIRE(badClass);
    REQUIRE(badClass->entityKind == "class");
    REQUIRE(badClass->suggestedName == "BadClass");
}

// case-catalogue follow-up: the entity kinds cpp/tags.janet's own widening
// added -- struct/enum/enum_member/field/macro/method/template_parameter --
// each gets its own violation, correctly kept apart from every sibling kind
// even when only ONE of them is configured.
TEST_CASE("End to end: struct/enum/enum_member/field/macro/method/template_parameter each "
          "reports under its own entity kind",
          "[FormatCase]") {
    const FormatRulesGuard guard;
    SetCaseConvention("struct", CaseConvention::PascalCase);
    SetCaseConvention("enum", CaseConvention::PascalCase);
    SetCaseConvention("enum_member", CaseConvention::ScreamingSnakeCase);
    SetCaseConvention("field", CaseConvention::SnakeCase);
    SetCaseConvention("macro", CaseConvention::ScreamingSnakeCase);
    SetCaseConvention("method", CaseConvention::CamelCase);
    SetCaseConvention("template_parameter", CaseConvention::PascalCase);

    const Mode        mode   = CppMode();
    const std::string source = "#define bad_macro 1\n"
                               "template <typename bad_param>\n"
                               "struct bad_struct {\n"
                               "    int Bad_Field;\n"
                               "    void Bad_Method() {}\n"
                               "};\n"
                               "enum bad_enum { bad_enumerator };\n";
    const auto violations = ComputeCaseViolations(source, "cpp", mode);

    const CaseViolation* badMacro = FindViolation(violations, "bad_macro");
    REQUIRE(badMacro);
    REQUIRE(badMacro->entityKind == "macro");
    REQUIRE(badMacro->suggestedName == "BAD_MACRO");

    const CaseViolation* badParam = FindViolation(violations, "bad_param");
    REQUIRE(badParam);
    REQUIRE(badParam->entityKind == "template_parameter");
    REQUIRE(badParam->suggestedName == "BadParam");

    const CaseViolation* badStruct = FindViolation(violations, "bad_struct");
    REQUIRE(badStruct);
    REQUIRE(badStruct->entityKind == "struct");
    REQUIRE(badStruct->suggestedName == "BadStruct");

    const CaseViolation* badField = FindViolation(violations, "Bad_Field");
    REQUIRE(badField);
    REQUIRE(badField->entityKind == "field");
    REQUIRE(badField->suggestedName == "bad_field");

    const CaseViolation* badMethod = FindViolation(violations, "Bad_Method");
    REQUIRE(badMethod);
    REQUIRE(badMethod->entityKind == "method");
    REQUIRE(badMethod->suggestedName == "badMethod");

    const CaseViolation* badEnum = FindViolation(violations, "bad_enum");
    REQUIRE(badEnum);
    REQUIRE(badEnum->entityKind == "enum");
    REQUIRE(badEnum->suggestedName == "BadEnum");

    const CaseViolation* badEnumerator = FindViolation(violations, "bad_enumerator");
    REQUIRE(badEnumerator);
    REQUIRE(badEnumerator->entityKind == "enum_member");
    REQUIRE(badEnumerator->suggestedName == "BAD_ENUMERATOR");
}

// case-catalogue follow-up: an in-class inline method used to be tagged
// "function" (the same entity kind as a free function) -- cpp/tags.janet's
// own field_identifier-declarator patterns now tag it "method" instead, so
// a "function" rule no longer touches it and a "method" rule does.
TEST_CASE("An in-class inline method is a 'method', not a 'function' -- the two rules don't "
          "cross-apply",
          "[FormatCase]") {
    const FormatRulesGuard guard;
    SetCaseConvention("function", CaseConvention::SnakeCase);

    const Mode        mode   = CppMode();
    const std::string source = "class Widget {\n"
                               "public:\n"
                               "    void BadMethodName() {}\n"
                               "};\n";
    const auto violations = ComputeCaseViolations(source, "cpp", mode);

    REQUIRE_FALSE(FindViolation(violations, "BadMethodName")); // "function" rule, "method" entity -- no cross-apply
}

TEST_CASE("CaseViolation's byte range points at exactly the name, not the whole declaration",
          "[FormatCase]") {
    const FormatRulesGuard guard;
    SetCaseConvention("function", CaseConvention::CamelCase);

    const Mode        mode   = CppMode();
    const std::string source = "void Bad_Name() {\n}\n";
    const auto        violations = ComputeCaseViolations(source, "cpp", mode);

    const CaseViolation* bad = FindViolation(violations, "Bad_Name");
    REQUIRE(bad);
    REQUIRE(source.substr(bad->nameStartByte, bad->nameEndByte - bad->nameStartByte) == "Bad_Name");
}

TEST_CASE("CaseRuleFor's language-scoped precedence is honored: a cpp-scoped override wins "
          "over the unscoped entity-kind rule",
          "[FormatCase]") {
    const FormatRulesGuard guard;
    SetCaseConvention("function", CaseConvention::SnakeCase);
    SetCaseConvention("cpp/function", CaseConvention::CamelCase);

    const Mode        mode   = CppMode();
    const std::string source = "void doThing() {\n}\n";
    // doThing matches camelCase (the cpp-scoped override) but not snake_case
    // (the unscoped fallback) -- reported only if the fallback won.
    REQUIRE(ComputeCaseViolations(source, "cpp", mode).empty());
}

// --- SuggestNameForConvention, direct unit coverage of the pure transform ---

TEST_CASE("SuggestNameForConvention returns the name unchanged when it already conforms",
          "[FormatCase]") {
    REQUIRE(SuggestNameForConvention("already_snake", CaseConvention::SnakeCase) == "already_snake");
    REQUIRE(SuggestNameForConvention("alreadyCamel", CaseConvention::CamelCase) == "alreadyCamel");
}

TEST_CASE("SuggestNameForConvention tokenizes on separators and camelCase transitions, then "
          "re-joins per the target convention",
          "[FormatCase]") {
    REQUIRE(SuggestNameForConvention("myBadName", CaseConvention::SnakeCase) == "my_bad_name");
    REQUIRE(SuggestNameForConvention("my_bad_name", CaseConvention::CamelCase) == "myBadName");
    REQUIRE(SuggestNameForConvention("my_bad_name", CaseConvention::PascalCase) == "MyBadName");
    REQUIRE(SuggestNameForConvention("MyBadName", CaseConvention::ScreamingSnakeCase) == "MY_BAD_NAME");
    REQUIRE(SuggestNameForConvention("MyBadName", CaseConvention::LispCase) == "my-bad-name");
    REQUIRE(SuggestNameForConvention("my_bad_name", CaseConvention::LeadingSnakeCase) == "My_bad_name");
    REQUIRE(SuggestNameForConvention("my_bad_name", CaseConvention::UpperSnakeCase) == "My_Bad_Name");
    REQUIRE(SuggestNameForConvention("MyBadName", CaseConvention::Lowercase) == "mybadname");
    REQUIRE(SuggestNameForConvention("my_bad_name", CaseConvention::Uppercase) == "MYBADNAME");
}

TEST_CASE("SuggestNameForConvention preserves digit runs as their own word-internal content",
          "[FormatCase]") {
    REQUIRE(SuggestNameForConvention("value123", CaseConvention::SnakeCase) == "value123");
    REQUIRE(SuggestNameForConvention("MAX_VALUE_123", CaseConvention::CamelCase) == "maxValue123");
}

TEST_CASE("SuggestNameForConvention returns empty for a name with no usable letters/digits",
          "[FormatCase]") {
    REQUIRE(SuggestNameForConvention("+", CaseConvention::SnakeCase).empty());
    REQUIRE(SuggestNameForConvention("___", CaseConvention::SnakeCase).empty());
}

TEST_CASE("SuggestNameForConvention against CaseConvention::None always returns the name "
          "unchanged (None matches everything)",
          "[FormatCase]") {
    REQUIRE(SuggestNameForConvention("Anything_Goes123", CaseConvention::None) == "Anything_Goes123");
}
