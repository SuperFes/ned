#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <optional>
#include <string>

#include <vector>

#include "Editor/Indent.h"
#include "Editor/IndentStyle.h"
#include "Editor/Mode.h"
#include "Editor/TreeSitter/IncrementalParse.h"
#include "Editor/TreeSitter/Languages.h"
#include "Editor/TreeSitter/Parser.h"
#include "Editor/TreeSitter/Query.h"
#include "Editor/TreeSitter/Tree.h"
#include "Text/Buffer.h"

using ned::editor::IndentColumnForLevel;
using ned::editor::IndentComputation;
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

// Existing callers only ever exercise Kind::Level results (Column results --
// @aligned -- get their own dedicated tests below) -- unwrap straight to the
// raw level so every pre-existing assertion in this file stays unchanged.
std::optional<int> LevelForLine(const Tree& tree, const std::string& text, const Query& query, std::size_t line,
                                const IndentStyle& style = IndentStyle{}) {
    const auto [lineStart, lineEnd]               = LineRange(text, line);
    const std::optional<IndentComputation> result = IndentLevelForLine(tree, text, query, lineStart, lineEnd, style);
    if (!result) {
        return std::nullopt;
    }
    REQUIRE(result->kind == IndentComputation::Kind::Level);
    return result->value;
}

// @aligned-paren-column-alignment follow-up: unlike LevelForLine above, does
// not assert a Kind -- callers below check for Kind::Column explicitly.
std::optional<IndentComputation> ComputationForLine(const Tree& tree, const std::string& text, const Query& query,
                                                    std::size_t line, const IndentStyle& style = IndentStyle{}) {
    const auto [lineStart, lineEnd] = LineRange(text, line);
    return IndentLevelForLine(tree, text, query, lineStart, lineEnd, style);
}

// @aligned-paren-column-alignment follow-up: mirrors kJsonIndentTestQuery
// above, but captures the array container "aligned" instead of "indent" --
// object stays a plain "indent" container so the nested-combine test below
// has something to nest inside an aligned container.
constexpr const char* kJsonAlignedIndentTestQuery = R"SCM(
(object) @indent
(array) @aligned
(object "}" @dedent)
(array "]" @dedent)
)SCM";

} // namespace

TEST_CASE("IndentLevelForLine returns 1 for a blank line freshly inside one indent-captured object", "[Indent]") {
    const auto language = LanguageByName("json");
    REQUIRE(language.has_value());
    const Parser      parser(*language);
    const Query       query(*language, kJsonIndentTestQuery);
    const std::string text = "{\n\n}\n";
    const Tree        tree = parser.Parse(text);

    const std::optional<int> level = LevelForLine(tree, text, query, 1); // the blank line
    REQUIRE(level.has_value());
    REQUIRE(*level == 1);
}

TEST_CASE("IndentLevelForLine does not double-count several containers opened on the same source line",
          "[Indent]") {
    const auto language = LanguageByName("json");
    REQUIRE(language.has_value());
    const Parser parser(*language);
    const Query  query(*language, kJsonIndentTestQuery);
    // object -> array -> object, all opened on line 0; line 1 continues
    // three levels deep syntactically but should read as ONE indent level,
    // since none of the three containers opened on a distinct source line.
    const std::string text = "{\"a\": [{\"b\":\n1}]}\n";
    const Tree        tree = parser.Parse(text);

    const std::optional<int> level = LevelForLine(tree, text, query, 1);
    REQUIRE(level.has_value());
    REQUIRE(*level == 1);
}

TEST_CASE("IndentLevelForLine counts two containers opened on genuinely different source lines", "[Indent]") {
    const auto language = LanguageByName("json");
    REQUIRE(language.has_value());
    const Parser      parser(*language);
    const Query       query(*language, kJsonIndentTestQuery);
    const std::string text = "{\n\"a\": [\n1\n]\n}\n";
    const Tree        tree = parser.Parse(text);

    const std::optional<int> level = LevelForLine(tree, text, query, 2); // the "1" line, inside object+array
    REQUIRE(level.has_value());
    REQUIRE(*level == 2);
}

TEST_CASE("IndentLevelForLine aligns an anonymous-token dedent capture with its opener's own line, not one level deeper",
          "[Indent]") {
    const auto language = LanguageByName("json");
    REQUIRE(language.has_value());
    const Parser      parser(*language);
    const Query       query(*language, kJsonIndentTestQuery);
    const std::string text = "{\n\"a\": 1\n}\n";
    const Tree        tree = parser.Parse(text);

    const std::optional<int> closingBraceLevel = LevelForLine(tree, text, query, 2); // "}"
    REQUIRE(closingBraceLevel.has_value());
    REQUIRE(*closingBraceLevel == 0); // aligns with line 0 ("{"), not one level deeper
}

TEST_CASE("IndentLevelForLine aligns a nested closing delimiter with its own opening line's level", "[Indent]") {
    const auto language = LanguageByName("json");
    REQUIRE(language.has_value());
    const Parser      parser(*language);
    const Query       query(*language, kJsonIndentTestQuery);
    const std::string text = "{\n\"a\": [\n1\n]\n}\n";
    const Tree        tree = parser.Parse(text);

    const std::optional<int> closingBracketLevel = LevelForLine(tree, text, query, 3); // "]"
    REQUIRE(closingBracketLevel.has_value());
    REQUIRE(*closingBracketLevel == 1); // matches line 1's own level ("\"a\": ["), not the array body's level 2
}

TEST_CASE("IndentLevelForLine returns level 0 everywhere when the query has no indent/dedent captures at all",
          "[Indent]") {
    const auto language = LanguageByName("json");
    REQUIRE(language.has_value());
    const Parser      parser(*language);
    const Query       query(*language, "(object)"); // no @indent/@dedent capture names at all
    const std::string text = "{\n\"a\": 1\n}\n";
    const Tree        tree = parser.Parse(text);

    for (std::size_t line = 0; line < 3; ++line) {
        const std::optional<int> level = LevelForLine(tree, text, query, line);
        REQUIRE(level.has_value());
        REQUIRE(*level == 0);
    }
}

TEST_CASE("IndentLevelForLine aligns a continuation line to the column right after an @aligned opener",
          "[Indent]") {
    const auto language = LanguageByName("json");
    REQUIRE(language.has_value());
    const Parser      parser(*language);
    const Query       query(*language, kJsonAlignedIndentTestQuery);
    const std::string text = "[1, 2,\n    3]\n";
    const Tree        tree = parser.Parse(text);

    const std::optional<IndentComputation> result = ComputationForLine(tree, text, query, 1); // "    3]"
    REQUIRE(result.has_value());
    REQUIRE(result->kind == IndentComputation::Kind::Column);
    REQUIRE(result->value == 1); // aligns under "1", the byte right after "["
}

TEST_CASE("IndentLevelForLine falls back to a plain indent level when an @aligned opener is alone on its own line",
          "[Indent]") {
    const auto language = LanguageByName("json");
    REQUIRE(language.has_value());
    const Parser      parser(*language);
    const Query       query(*language, kJsonAlignedIndentTestQuery);
    const std::string text = "[\n1\n]\n";
    const Tree        tree = parser.Parse(text);

    const std::optional<IndentComputation> result = ComputationForLine(tree, text, query, 1); // "1"
    REQUIRE(result.has_value());
    REQUIRE(result->kind == IndentComputation::Kind::Level);
    REQUIRE(result->value == 1); // nothing to align to -- behaves exactly like @indent
}

TEST_CASE("IndentLevelForLine combines a nested indent level with its enclosing @aligned column", "[Indent]") {
    const auto language = LanguageByName("json");
    REQUIRE(language.has_value());
    const Parser parser(*language);
    const Query  query(*language, kJsonAlignedIndentTestQuery);
    // The object opens right after "[" on line 0 (so it aligns to column 1,
    // same as the plain-alignment case above); its own body should still
    // indent one level deeper than ITS OWN column, not from column zero.
    const std::string text = "[{\"a\": 1,\n\"b\": 2}]\n";
    const Tree        tree = parser.Parse(text);
    const IndentStyle style{.useTabs = false, .width = 4};

    const std::optional<IndentComputation> result = ComputationForLine(tree, text, query, 1, style); // "\"b\": 2}]"
    REQUIRE(result.has_value());
    REQUIRE(result->kind == IndentComputation::Kind::Column);
    REQUIRE(result->value == 5); // column 1 (the object's own aligned column) + one level (4)
}

TEST_CASE("IndentColumnForLevel respects IndentStyle::width", "[Indent]") {
    REQUIRE(IndentColumnForLevel(0, IndentStyle{.useTabs = false, .width = 4}) == 0);
    REQUIRE(IndentColumnForLevel(1, IndentStyle{.useTabs = false, .width = 4}) == 4);
    REQUIRE(IndentColumnForLevel(3, IndentStyle{.useTabs = false, .width = 2}) == 6);
}

// ---------------------------------------------------------------------------
// Verbatim regions: a multi-line string's interior is its VALUE, and a
// reindent must not touch it. `indent-buffer` used to rewrite all three of
// these, in every language that has such a construct, and the parse tree came
// out identical afterwards -- which is why the structural-safety property
// could not see it and these are written against the text itself.
// ---------------------------------------------------------------------------

namespace {

std::size_t ReindentWhole(ned::text::Buffer& buffer, const ned::editor::Mode& mode) {
    return ned::editor::IndentRegion(buffer, mode, 0, buffer.Content().LineCount());
}

} // namespace

TEST_CASE("Reindenting leaves a Python docstring's interior byte-for-byte", "[Indent]") {
    const std::string source = "def f():\n    s = \"\"\"\n  ragged inside\n        deeper inside\n\"\"\"\n    return s\n";
    ned::text::Buffer buffer("t.py");
    buffer.InsertAtPoint(source);

    CHECK(ReindentWhole(buffer, ned::editor::PythonMode()) == 0);
    CHECK(buffer.Text() == source);
}

TEST_CASE("Reindenting leaves a C++ raw string literal byte-for-byte", "[Indent]") {
    // R"(...)" exists precisely so its bytes are what they say they are.
    const std::string source = "int main() {\n    const char* s = R\"(\n  ragged\n        deeper\n)\";\n    return 0;\n}\n";
    ned::text::Buffer buffer("t.cpp");
    buffer.InsertAtPoint(source);

    CHECK(ReindentWhole(buffer, ned::editor::CppMode()) == 0);
    CHECK(buffer.Text() == source);
}

TEST_CASE("Reindenting leaves a PHP heredoc byte-for-byte", "[Indent]") {
    const std::string source =
        "<?php\nfunction f() {\n    $s = <<<EOT\n  ragged\n        deeper\nEOT;\n    return $s;\n}\n";
    ned::text::Buffer buffer("t.php");
    buffer.InsertAtPoint(source);

    CHECK(ReindentWhole(buffer, ned::editor::PhpMode()) == 0);
    CHECK(buffer.Text() == source);
}

TEST_CASE("The line a multi-line string opens on is ordinary code and still indents", "[Indent]") {
    // The rule protects the INTERIOR, not the construct: getting this wrong in
    // the other direction would quietly stop reindenting any line that happens
    // to introduce a string.
    ned::text::Buffer buffer("t.py");
    buffer.InsertAtPoint("def f():\n        s = \"\"\"\n  body\n\"\"\"\n");

    CHECK(ReindentWhole(buffer, ned::editor::PythonMode()) == 1);
    CHECK(buffer.Text() == "def f():\n    s = \"\"\"\n  body\n\"\"\"\n");
}

TEST_CASE("VerbatimRanges reports only strings that cross a line", "[Indent]") {
    // A single-line string can never contain a line start strictly inside it,
    // so carrying it would only make the per-line check longer.
    const auto mode   = ned::editor::PythonMode();
    const auto ranges = ned::editor::VerbatimRanges(mode, "a = \"one line\"\nb = \"\"\"two\nlines\"\"\"\n");
    REQUIRE(ranges.size() == 1);
    CHECK(ranges[0].first == 19);
}

TEST_CASE("LineIsVerbatim is strictly inside, at both ends", "[Indent]") {
    const std::vector<std::pair<std::size_t, std::size_t>> ranges = {{10, 20}};
    CHECK_FALSE(ned::editor::LineIsVerbatim(ranges, 10)); // the opening line is code
    CHECK(ned::editor::LineIsVerbatim(ranges, 11));
    CHECK(ned::editor::LineIsVerbatim(ranges, 19)); // the closer's line carries string bytes
    CHECK_FALSE(ned::editor::LineIsVerbatim(ranges, 20));
}

TEST_CASE("A mode with no highlighter has no verbatim regions and still indents", "[Indent]") {
    ned::editor::Mode mode;
    mode.indentColumn = [](std::string_view, std::size_t, std::size_t) { return std::optional<int>(7); };
    CHECK(ned::editor::VerbatimRanges(mode, "anything").empty());
    CHECK(ned::editor::IndentColumnForLine(mode, "anything", 0, 8) == 7);
}

// ---------------------------------------------------------------------------
// JSX indents by matched tags, which no bracket fact can supply.
//
// Asserted as agreement with already-correct code: every line below is written
// the way a JSX author would write it, and the engine has to arrive at the same
// column for each one. Before these rules existed the whole tree of elements
// computed one flat level -- .tsx and .jsx files simply had no JSX indentation.
// ---------------------------------------------------------------------------

namespace {

void CheckAgreesWithWrittenIndent(const ned::editor::Mode& mode, const std::string& text) {
    std::size_t lineStart = 0;
    std::size_t lineNo    = 0;
    while (lineStart < text.size()) {
        const std::size_t nl      = text.find('\n', lineStart);
        const std::size_t lineEnd = (nl == std::string::npos) ? text.size() : nl;
        const std::size_t written = text.find_first_not_of(' ', lineStart) - lineStart;
        INFO("line " << lineNo << ": " << text.substr(lineStart, lineEnd - lineStart));
        CHECK(mode.indentColumn(text, lineStart, lineEnd).value_or(-1) == static_cast<int>(written));
        if (nl == std::string::npos)
            break;
        lineStart = nl + 1;
        ++lineNo;
    }
}

} // namespace

TEST_CASE("TSX indents JSX elements, expressions and closing tags", "[Indent]") {
    CheckAgreesWithWrittenIndent(ned::editor::TsxMode(),
                                 "export function Panel(props: Props) {\n"
                                 "    const items = props.items.map((item) => (\n"
                                 "        <li key={item.id}>\n"
                                 "            {item.label}\n"
                                 "        </li>\n"
                                 "    ));\n"
                                 "    return (\n"
                                 "        <ul className=\"panel\">\n"
                                 "            {items}\n"
                                 "        </ul>\n"
                                 "    );\n"
                                 "}\n");
}

TEST_CASE("A multi-line JSX attribute list indents its own attributes", "[Indent]") {
    // The opening tag is a container in its own right, and its ">" closes it.
    CheckAgreesWithWrittenIndent(ned::editor::TsxMode(),
                                 "const view = (\n"
                                 "    <section\n"
                                 "        className=\"wide\"\n"
                                 "        onClick={handler}\n"
                                 "    >\n"
                                 "        <Item />\n"
                                 "    </section>\n"
                                 ");\n");
}

TEST_CASE("JSX in a plain .jsx file indents the same way", "[Indent]") {
    // javascript-indents.scm carries the same rules: JSX is not TypeScript's.
    CheckAgreesWithWrittenIndent(ned::editor::JavaScriptMode(),
                                 "function App() {\n"
                                 "    return (\n"
                                 "        <div>\n"
                                 "            <span>hello</span>\n"
                                 "        </div>\n"
                                 "    );\n"
                                 "}\n");
}
