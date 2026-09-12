// The delimiter imprint as an indent source (Editor/ImprintIndent.h): what it
// contributes, what it declines, and how a query says minus.
#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>
#include <string_view>

#include "Editor/CodeFold.h"
#include "Editor/Indent.h"
#include "Editor/IndentStyle.h"
#include "Editor/Mode.h"
#include "Editor/TreeSitter/Languages.h"
#include "Editor/TreeSitter/Parser.h"
#include "Editor/TreeSitter/Query.h"
#include "Editor/TreeSitter/Tree.h"

using ned::editor::EffectiveIndentStyle;
using ned::editor::Mode;

namespace {

// [lineStart, lineEnd) of `text`'s 0-indexed `line`'th line, newline excluded.
std::pair<std::size_t, std::size_t> LineRange(std::string_view text, std::size_t line) {
    std::size_t start = 0;
    for (std::size_t i = 0; i < line; ++i) {
        start = text.find('\n', start) + 1;
    }
    const std::size_t end = text.find('\n', start);
    return {start, end == std::string_view::npos ? text.size() : end};
}

std::optional<int> ColumnOf(const Mode& mode, std::string_view text, std::size_t line) {
    REQUIRE(mode.indentColumn);
    const auto [start, end] = LineRange(text, line);
    return ned::editor::IndentColumnForLine(mode, text, start, end);
}

int Width(const Mode& mode) {
    return EffectiveIndentStyle(mode.name).width;
}

} // namespace

TEST_CASE("A bracket body with content before its bracket counts for its continuation lines only",
          "[Indent][Imprint]") {
    // `a[i]` starts at `a`, so the subscript is an ancestor of the line it
    // opens on. Counting it there indented every subscript assignment one
    // level too deep -- the reason no hand-written query ever captured a
    // subscript, and the reason a multi-line one never indented at all.
    const Mode        mode = ned::editor::JavaScriptMode();
    const std::string text = "function f() {\n"
                             "  a[i] = 1;\n"
                             "  b = c[\n"
                             "    1\n"
                             "  ];\n"
                             "}\n";
    const int         w    = Width(mode);
    CHECK(ColumnOf(mode, text, 1) == w);     // a[i] = 1;  -- the subscript does not count here
    CHECK(ColumnOf(mode, text, 3) == 2 * w); // 1          -- inside c[ ... ]
    CHECK(ColumnOf(mode, text, 4) == w);     // ];         -- the closer aligns with its opener's line
}

TEST_CASE("Containers the hand-written queries lacked indent from the imprint", "[Indent][Imprint]") {
    // Measured over the grammar repos' own example files: every line the
    // imprint changed was one the query had left flat and the file had
    // indented. These are the three that showed up.
    {
        const Mode        go   = ned::editor::GoMode();
        const std::string text = "import (\n\"fmt\"\n)\n";
        CHECK(ColumnOf(go, text, 1) == Width(go));
        CHECK(ColumnOf(go, text, 2) == 0);
    }
    {
        const Mode        kotlin = ned::editor::KotlinMode();
        const std::string text   = "class Logger(\nval level: Int\n)\n";
        CHECK(ColumnOf(kotlin, text, 1) == Width(kotlin));
    }
    {
        const Mode        rust = ned::editor::RustMode();
        const std::string text = "fn f() {\n    write!(\nf,\n\"x\"\n);\n}\n";
        const int         w    = Width(rust);
        CHECK(ColumnOf(rust, text, 2) == 2 * w); // inside the macro's token tree
        CHECK(ColumnOf(rust, text, 4) == w);     // `);` closes it
    }
}

TEST_CASE("An indentation body indented relative to nothing is a root, not a container", "[Indent][Imprint]") {
    const Mode yaml = ned::editor::YamlMode();
    // The document's own mapping, at column zero and indented.
    CHECK(ColumnOf(yaml, "a: 1\nb: 2\n", 1) == 0);
    CHECK(ColumnOf(yaml, "  a: 1\n  b: 2\n", 1) == 0);
    // A nested mapping has `a:` above it, shallower.
    CHECK(ColumnOf(yaml, "a:\n  b: 1\n  c: 2\n", 2) == Width(yaml));
}

TEST_CASE("A mid-line indentation body has its header on its own row", "[Indent][Imprint]") {
    // `- key: v` -- the mapping begins after the dash, and its later lines
    // sit under the key.
    const Mode yaml = ned::editor::YamlMode();
    CHECK(ColumnOf(yaml, "- key: v\n  other: w\n", 1) == Width(yaml));
}

TEST_CASE("A comment between a header and its body does not hide the header", "[Indent][Imprint][CodeFold]") {
    const Mode        python = ned::editor::PythonMode();
    const std::string text   = "def f():\n"
                               "    # about x\n"
                               "    x = 1\n";
    CHECK(ColumnOf(python, text, 2) == Width(python));
    // The same header search anchors the fold: it starts on the `def` row,
    // not on the comment's or the statement's.
    const auto blocks = ned::editor::codefold::FoldableBlocks(python, text);
    REQUIRE(blocks.size() == 1);
    CHECK(blocks.front().first == 0);
}

TEST_CASE("@indent.suppress withdraws the imprint's container, and a query capture re-asserts it",
          "[Indent][Imprint]") {
    const auto language = ned::editor::treesitter::LanguageByName("json");
    REQUIRE(language.has_value());
    const ned::editor::treesitter::Parser parser(*language);
    const std::string                     text = "{\n\"a\": 1\n}\n";
    const ned::editor::treesitter::Tree   tree = parser.Parse(text);
    const ned::editor::IndentStyle        style{.width = 4, .useTabs = false};
    const auto [start, end] = LineRange(text, 1);

    const auto levelWith = [&](const char* source) {
        const ned::editor::treesitter::Query query(*language, source);
        ned::editor::IndentCaptures          captures = ned::editor::IndentCapturesFromQuery(tree, text, query);
        ned::editor::AddImprintCaptures(captures, tree, "json", text);
        const auto result = ned::editor::IndentLevelForLine(tree, text, captures, start, end, style);
        REQUIRE(result.has_value());
        REQUIRE(result->kind == ned::editor::IndentComputation::Kind::Level);
        return result->value;
    };

    CHECK(levelWith("") == 1);                                           // the imprint alone
    CHECK(levelWith("(object) @indent.suppress") == 0);                  // withdrawn
    CHECK(levelWith("(object) @indent.suppress (object) @indent") == 1); // the query's own assertion stands
}

TEST_CASE("A keyword-delimited body indents from the imprint with no capture", "[Indent][Imprint]") {
    // bash and fish carry no @indent capture any more; `do ... done` and
    // `if ... end` are delimited bodies the same as `{ ... }`, and the closer
    // dedents the same way. The clause headers (`else`) are the only thing
    // still declared, and they still align with the `if`.
    {
        const Mode        bash = ned::editor::BashMode();
        const std::string text = "for f in *; do\necho $f\ndone\nif true; then\na\nelse\nb\nfi\n";
        const int         w    = Width(bash);
        CHECK(ColumnOf(bash, text, 1) == w); // echo $f
        CHECK(ColumnOf(bash, text, 2) == 0); // done
        CHECK(ColumnOf(bash, text, 4) == w); // a
        CHECK(ColumnOf(bash, text, 5) == 0); // else
        CHECK(ColumnOf(bash, text, 6) == w); // b
        CHECK(ColumnOf(bash, text, 7) == 0); // fi
    }
    {
        const Mode        fish = ned::editor::FishMode();
        const std::string text = "function greet\nif test -n \"$argv\"\necho hi\nelse\necho bye\nend\nend\n";
        const int         w    = Width(fish);
        CHECK(ColumnOf(fish, text, 1) == w);     // if ...
        CHECK(ColumnOf(fish, text, 2) == 2 * w); // echo hi
        CHECK(ColumnOf(fish, text, 3) == w);     // else
        CHECK(ColumnOf(fish, text, 5) == w);     // end (of if)
        CHECK(ColumnOf(fish, text, 6) == 0);     // end (of function)
    }
}

TEST_CASE("A sigil-prefixed bracket body indents from the imprint", "[Indent][Imprint]") {
    // Janet's `@[...]` was the last thing janet-indents.scm still declared.
    const Mode        janet = ned::editor::JanetMode();
    const std::string text  = "@[\n1\n2]\n";
    CHECK(ColumnOf(janet, text, 1) == Width(janet));
    CHECK(ColumnOf(janet, text, 2) == Width(janet)); // `2]` is content, not a closer line

    const Mode        bash = ned::editor::BashMode();
    const std::string sub  = "x=$(\nls\n)\n";
    CHECK(ColumnOf(bash, sub, 1) == Width(bash));
    CHECK(ColumnOf(bash, sub, 2) == 0);
}
