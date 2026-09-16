//
// ned/set-indent-rule: the config-storage half (SetIndentRule/IndentRuleFor,
// unscoped and language-scoped resolution) and the real end-to-end pilot on
// C++ -- both of this feature's own worked examples from ROADMAP.md's
// design sketch: an access specifier's -2 offset from its class body, and a
// preprocessor directive's absolute, nesting-independent column 0.
//

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>
#include <string_view>

#include "Editor/Indent.h"
#include "Editor/IndentRuleOverride.h"
#include "Editor/IndentStyle.h"
#include "Editor/Mode.h"

using ned::editor::CppMode;
using ned::editor::EffectiveIndentStyle;
using ned::editor::IndentColumnForLine;
using ned::editor::IndentRuleFor;
using ned::editor::IndentRulePolicy;
using ned::editor::IndentRuleValue;
using ned::editor::Mode;
using ned::editor::SetIndentRule;

namespace {

struct IndentRuleGuard {
    ~IndentRuleGuard() {
        SetIndentRule("access_specifier", std::nullopt);
        SetIndentRule("cpp/access_specifier", std::nullopt);
        SetIndentRule("python/access_specifier", std::nullopt);
        SetIndentRule("preproc_def", std::nullopt);
    }
};

std::pair<std::size_t, std::size_t> LineRange(std::string_view text, std::size_t line) {
    std::size_t start = 0;
    for (std::size_t i = 0; i < line; ++i) {
        start = text.find('\n', start) + 1;
    }
    const std::size_t end = text.find('\n', start);
    return {start, end == std::string_view::npos ? text.size() : end};
}

std::optional<int> ColumnOf(const Mode& mode, std::string_view text, std::size_t line) {
    const auto [start, end] = LineRange(text, line);
    return IndentColumnForLine(mode, text, start, end);
}

int Width(const Mode& mode) {
    return EffectiveIndentStyle(mode.name).width;
}

} // namespace

TEST_CASE("SetIndentRule/IndentRuleFor stores and clears independently of any language scope", "[IndentRuleOverride]") {
    const IndentRuleGuard guard;
    REQUIRE_FALSE(IndentRuleFor("access_specifier").policy.has_value());

    SetIndentRule("access_specifier", IndentRuleValue{IndentRulePolicy::Offset, -2});
    const IndentRuleValue value = IndentRuleFor("access_specifier");
    REQUIRE(value.policy == IndentRulePolicy::Offset);
    REQUIRE(value.value == -2);

    SetIndentRule("access_specifier", std::nullopt);
    REQUIRE_FALSE(IndentRuleFor("access_specifier").policy.has_value());
}

TEST_CASE("IndentRuleFor's language-scoped overload prefers \"<language>/<key>\" over the bare key",
          "[IndentRuleOverride]") {
    const IndentRuleGuard guard;
    SetIndentRule("access_specifier", IndentRuleValue{IndentRulePolicy::Offset, -1});
    SetIndentRule("cpp/access_specifier", IndentRuleValue{IndentRulePolicy::Offset, -2});

    REQUIRE(IndentRuleFor("access_specifier", "cpp").value == -2);   // scoped wins
    REQUIRE(IndentRuleFor("access_specifier", "python").value == -1); // falls back to the bare key
    REQUIRE(IndentRuleFor("access_specifier", "").value == -1);       // no language given at all
}

TEST_CASE("SetIndentRule rejects a key shaped like an invalid capture name", "[IndentRuleOverride]") {
    REQUIRE_THROWS(SetIndentRule("", IndentRuleValue{IndentRulePolicy::Offset, 0}));
    REQUIRE_THROWS(SetIndentRule("@access_specifier", IndentRuleValue{IndentRulePolicy::Offset, 0}));
    REQUIRE_THROWS(SetIndentRule("access specifier", IndentRuleValue{IndentRulePolicy::Offset, 0}));
}

TEST_CASE("cpp-mode indents an access specifier normally with no rule configured", "[IndentRuleOverride][Indent]") {
    const Mode        mode = CppMode();
    const std::string text = "class C {\npublic:\n    int x;\n};\n";
    REQUIRE(ColumnOf(mode, text, 1) == Width(mode)); // one level in, same as any other class-body line
}

TEST_CASE("ned/set-indent-rule offsets an access specifier -2 columns from its class body", "[IndentRuleOverride][Indent]") {
    const IndentRuleGuard guard;
    SetIndentRule("access_specifier", IndentRuleValue{IndentRulePolicy::Offset, -2});

    const Mode        mode = CppMode();
    const std::string text = "class C {\npublic:\n    int x;\n};\n";

    REQUIRE(ColumnOf(mode, text, 1) == Width(mode) - 2); // "public:" -- offset applied
    REQUIRE(ColumnOf(mode, text, 2) == Width(mode));     // "int x;" -- an ordinary member, untouched
}

TEST_CASE("ned/set-indent-rule's offset applies to every access specifier in a class, nested or not",
          "[IndentRuleOverride][Indent]") {
    const IndentRuleGuard guard;
    SetIndentRule("access_specifier", IndentRuleValue{IndentRulePolicy::Offset, -2});

    const Mode        mode = CppMode();
    const std::string text = "class C {\npublic:\n    int x;\nprivate:\n    int y;\n};\n";

    REQUIRE(ColumnOf(mode, text, 1) == Width(mode) - 2); // public:
    REQUIRE(ColumnOf(mode, text, 3) == Width(mode) - 2); // private:
}

TEST_CASE("ned/set-indent-rule's language scoping means an unrelated language's own override never fires",
          "[IndentRuleOverride][Indent]") {
    const IndentRuleGuard guard;
    SetIndentRule("python/access_specifier", IndentRuleValue{IndentRulePolicy::Offset, -2}); // a different language

    const Mode        mode = CppMode();
    const std::string text = "class C {\npublic:\n    int x;\n};\n";
    REQUIRE(ColumnOf(mode, text, 1) == Width(mode)); // cpp's own access_specifier is unaffected
}

TEST_CASE("ned/set-indent-rule's absolute policy roots a preprocessor directive at a fixed column regardless of "
          "nesting depth",
          "[IndentRuleOverride][Indent]") {
    const IndentRuleGuard guard;
    SetIndentRule("preproc_def", IndentRuleValue{IndentRulePolicy::Absolute, 0});

    const Mode        mode = CppMode();
    const std::string text = "void f() {\n    if (x) {\n#define FOO 1\n    }\n}\n";

    // Without the rule, a #define this deep inside nested blocks would
    // otherwise come out two levels in -- confirm the override actually
    // overrides something real, not a construct that was already at 0.
    SetIndentRule("preproc_def", std::nullopt);
    REQUIRE(ColumnOf(mode, text, 2) != 0);

    SetIndentRule("preproc_def", IndentRuleValue{IndentRulePolicy::Absolute, 0});
    REQUIRE(ColumnOf(mode, text, 2) == 0);
    REQUIRE(ColumnOf(mode, text, 3) == Width(mode)); // the closing "}" of the if is unaffected
}
