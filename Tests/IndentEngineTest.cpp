#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <optional>
#include <string>

#include "Editor/Indent.h"
#include "Editor/IndentStyle.h"
#include "Editor/TreeSitter/IncrementalParse.h"
#include "Editor/TreeSitter/Languages.h"
#include "Editor/TreeSitter/Parser.h"
#include "Editor/TreeSitter/Query.h"
#include "Editor/TreeSitter/Tree.h"

using ned::editor::IndentColumnForLevel;
using ned::editor::IndentLevelForLine;
using ned::editor::IndentStyle;
using ned::editor::treesitter::IncrementalParseCache;
using ned::editor::treesitter::LanguageByName;
using ned::editor::treesitter::Parser;
using ned::editor::treesitter::Query;
using ned::editor::treesitter::Tree;

namespace {

// Isolates the generic tree-walk algorithm from any per-language query
// file's own real-world quirks (Tests/IndentTest.cpp covers those) by
// running a small, hand-written "indent"/"dedent"-capture query directly
// against JsonMode's own real (deliberately simple) bundled grammar --
// giving full control over exactly which nodes are captured without
// needing a from-scratch synthetic grammar.
constexpr const char* kJsonIndentTestQuery = R"SCM(
(object) @indent
(array) @indent
(object "}" @dedent)
(array "]" @dedent)
)SCM";

// [lineStart, lineEnd) of the 0-indexed `line`'th '\n'-delimited line in
// text, excluding its own trailing newline -- a small local helper so each
// test case can just name a line by index rather than hand-counting bytes.
std::pair<std::size_t, std::size_t> LineRange(const std::string& text, std::size_t line) {
    std::size_t start = 0;
    for (std::size_t i = 0; i < line; ++i) {
        start = text.find('\n', start) + 1;
    }
    std::size_t end = text.find('\n', start);
    if (end == std::string::npos) {
        end = text.size();
    }
    return {start, end};
}

std::optional<int> LevelForLine(const Tree& tree, const std::string& text, const Query& query, std::size_t line) {
    const auto [lineStart, lineEnd] = LineRange(text, line);
    return IndentLevelForLine(tree, text, query, lineStart, lineEnd);
}

} // namespace

TEST_CASE("IndentLevelForLine returns 1 for a blank line freshly inside one indent-captured object", "[Indent]") {
    const auto   language = LanguageByName("json");
    REQUIRE(language.has_value());
    const Parser  parser(*language);
    const Query   query(*language, kJsonIndentTestQuery);
    const std::string text = "{\n\n}\n";
    const Tree    tree = parser.Parse(text);

    const std::optional<int> level = LevelForLine(tree, text, query, 1); // the blank line
    REQUIRE(level.has_value());
    REQUIRE(*level == 1);
}

TEST_CASE("IndentLevelForLine does not double-count several containers opened on the same source line",
          "[Indent]") {
    const auto   language = LanguageByName("json");
    REQUIRE(language.has_value());
    const Parser  parser(*language);
    const Query   query(*language, kJsonIndentTestQuery);
    // object -> array -> object, all opened on line 0; line 1 continues
    // three levels deep syntactically but should read as ONE indent level,
    // since none of the three containers opened on a distinct source line.
    const std::string text = "{\"a\": [{\"b\":\n1}]}\n";
    const Tree    tree = parser.Parse(text);

    const std::optional<int> level = LevelForLine(tree, text, query, 1);
    REQUIRE(level.has_value());
    REQUIRE(*level == 1);
}

TEST_CASE("IndentLevelForLine counts two containers opened on genuinely different source lines", "[Indent]") {
    const auto   language = LanguageByName("json");
    REQUIRE(language.has_value());
    const Parser  parser(*language);
    const Query   query(*language, kJsonIndentTestQuery);
    const std::string text = "{\n\"a\": [\n1\n]\n}\n";
    const Tree    tree = parser.Parse(text);

    const std::optional<int> level = LevelForLine(tree, text, query, 2); // the "1" line, inside object+array
    REQUIRE(level.has_value());
    REQUIRE(*level == 2);
}

TEST_CASE("IndentLevelForLine aligns an anonymous-token dedent capture with its opener's own line, not one level deeper",
          "[Indent]") {
    const auto   language = LanguageByName("json");
    REQUIRE(language.has_value());
    const Parser  parser(*language);
    const Query   query(*language, kJsonIndentTestQuery);
    const std::string text = "{\n\"a\": 1\n}\n";
    const Tree    tree = parser.Parse(text);

    const std::optional<int> closingBraceLevel = LevelForLine(tree, text, query, 2); // "}"
    REQUIRE(closingBraceLevel.has_value());
    REQUIRE(*closingBraceLevel == 0); // aligns with line 0 ("{"), not one level deeper
}

TEST_CASE("IndentLevelForLine aligns a nested closing delimiter with its own opening line's level", "[Indent]") {
    const auto   language = LanguageByName("json");
    REQUIRE(language.has_value());
    const Parser  parser(*language);
    const Query   query(*language, kJsonIndentTestQuery);
    const std::string text = "{\n\"a\": [\n1\n]\n}\n";
    const Tree    tree = parser.Parse(text);

    const std::optional<int> closingBracketLevel = LevelForLine(tree, text, query, 3); // "]"
    REQUIRE(closingBracketLevel.has_value());
    REQUIRE(*closingBracketLevel == 1); // matches line 1's own level ("\"a\": ["), not the array body's level 2
}

TEST_CASE("IndentLevelForLine returns level 0 everywhere when the query has no indent/dedent captures at all",
          "[Indent]") {
    const auto   language = LanguageByName("json");
    REQUIRE(language.has_value());
    const Parser  parser(*language);
    const Query   query(*language, "(object)"); // no @indent/@dedent capture names at all
    const std::string text = "{\n\"a\": 1\n}\n";
    const Tree    tree = parser.Parse(text);

    for (std::size_t line = 0; line < 3; ++line) {
        const std::optional<int> level = LevelForLine(tree, text, query, line);
        REQUIRE(level.has_value());
        REQUIRE(*level == 0);
    }
}

TEST_CASE("IndentColumnForLevel respects IndentStyle::width", "[Indent]") {
    REQUIRE(IndentColumnForLevel(0, IndentStyle{.useTabs = false, .width = 4}) == 0);
    REQUIRE(IndentColumnForLevel(1, IndentStyle{.useTabs = false, .width = 4}) == 4);
    REQUIRE(IndentColumnForLevel(3, IndentStyle{.useTabs = false, .width = 2}) == 6);
}
