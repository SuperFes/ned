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
#include "Editor/Grammar/Languages.h"
#include "Editor/Grammar/Parser.h"
#include "Editor/Grammar/QueryMatcher.h"
#include "Editor/Grammar/Tree.h"

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
    const auto language = ned::editor::grammar::LanguageByName("json");
    REQUIRE(language.has_value());
    const ned::editor::grammar::Parser parser(*language);
    const std::string                     text = "{\n\"a\": 1\n}\n";
    const ned::editor::grammar::Tree   tree = parser.Parse(text);
    const ned::editor::IndentStyle        style{.useTabs = false, .width = 4};
    const auto [start, end] = LineRange(text, 1);

    const auto levelWith = [&](const char* source) {
        const ned::editor::grammar::QueryMatcher query(*language, source);
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

TEST_CASE("A for-loop header's own parens indent a continuation line, without leaking into its body",
          "[Indent][Imprint]") {
    // for-loop-header-imprint follow-up, reported live against this
    // project's own main.cpp. Two distinct bugs, both from the SAME root
    // cause (for_statement/for_range_loop's own "(...)" trails its closer
    // with a required `body` field, GrammarImprint.cpp's MatchBracketed):
    //  1. Classifying for_statement/for_range_loop at all (they had NO
    //     imprint entry before) is what makes line 2 below indent one level
    //     past "for" itself (level 2 overall: f()'s body, then the for
    //     header's own continuation) -- previously it landed flush with
    //     "for" (level 1).
    //  2. Bounding that container's own contribution to the closer's end
    //     (Indent.h's IndentCaptures::interiorEnd), not the node's full
    //     span, is what keeps the for-loop's OWN {...} body (lines 3-8) at
    //     its own correct level (2: f()'s body, then the for's compound
    //     statement body) rather than 3 -- an earlier version of this fix
    //     let the for's own header container reach all the way through
    //     that body, double counting every line in it.
    const Mode        cpp  = ned::editor::CppMode();
    const std::string text = "void f() {\n"
                             "    for (const std::filesystem::path& p :\n"
                             "        Directories(x)) {\n"
                             "        try {\n"
                             "            g();\n"
                             "        }\n"
                             "        catch (const std::exception& e) {\n"
                             "            h();\n"
                             "        }\n"
                             "    }\n"
                             "}\n";
    const int w = Width(cpp);
    CHECK(ColumnOf(cpp, text, 2) == 2 * w); // "Directories(x)) {" -- one level past "for" itself
    CHECK(ColumnOf(cpp, text, 3) == 2 * w); // "try {" -- level 2, not 3
    CHECK(ColumnOf(cpp, text, 4) == 3 * w); // "g();"
    CHECK(ColumnOf(cpp, text, 5) == 2 * w); // the try's own "}"
    CHECK(ColumnOf(cpp, text, 6) == 2 * w); // "catch (...) {"
    CHECK(ColumnOf(cpp, text, 9) == w);     // the for's own closing "}", aligned with the "for" line itself
}

TEST_CASE("Lua's repeat_statement body indents from its own hand-authored query",
          "[Indent][Imprint]") {
    // ROADMAP.md watch-list entry: repeat_statement's grammar rule is
    // SEQ["repeat", body, "until", condition] -- "until" is not the rule's
    // own last member (a required condition follows it), so
    // MatchKeywordPair's front/back check never finds it the way it finds
    // do/if/while/for/function's own trailing "end". Fixed via
    // lua/indents.janet's `(repeat_statement "until")` pair rather than the
    // static inference tool.
    const Mode        lua  = ned::editor::LuaMode();
    const std::string text = "repeat\ni = i + 1\nuntil i >= 3\nprint(i)\n";
    const int         w    = Width(lua);
    CHECK(ColumnOf(lua, text, 1) == w); // i = i + 1
    CHECK(ColumnOf(lua, text, 2) == 0); // until i >= 3 -- aligns with "repeat"
    CHECK(ColumnOf(lua, text, 3) == 0); // print(i) -- outside the loop entirely
}

TEST_CASE("Go's switch/select case and default clauses align with their own switch/select",
          "[Indent][Imprint]") {
    // ROADMAP.md watch-list entry: expression_case/default_case/type_case/
    // communication_case carry no delimiters of their own, so every line
    // starting inside a switch/select body's braces -- including the case
    // labels themselves -- got the same single container level, one deeper
    // than gofmt's own convention of aligning a case label back with its
    // switch. Fixed via go/indents.janet's @dedent on the four clause node
    // types, mirroring bash/python's own elif_clause/else_clause dedent.
    const Mode        go   = ned::editor::GoMode();
    const std::string text = "switch x {\ncase 1:\nfoo()\ndefault:\nbar()\n}\n";
    const int         w    = Width(go);
    CHECK(ColumnOf(go, text, 1) == 0);     // case 1: -- aligns with "switch"
    CHECK(ColumnOf(go, text, 2) == w);     // foo()
    CHECK(ColumnOf(go, text, 3) == 0);     // default: -- aligns with "switch"
    CHECK(ColumnOf(go, text, 4) == w);     // bar()
    CHECK(ColumnOf(go, text, 5) == 0);     // the switch's own closing "}"
}

TEST_CASE("PHP's colon-alternate if/elseif/else/endif indents like the brace form",
          "[Indent][Imprint]") {
    // ROADMAP.md watch-list entry: if_statement's colon-form body
    // (`colon_block`, closed by a literal "endif" belonging to
    // if_statement itself) has nothing in ImprintTables.cpp to key off, so
    // every line -- body content and elseif/else headers alike -- sat at
    // column 0. Fixed via php/indents.janet's `(if_statement "endif")`
    // pair (present only on the colon form, so the brace form -- whose
    // if_statement has no "endif" child -- is untouched) plus an
    // unconditional @dedent on else_if_clause/else_clause, which is a
    // no-op for the brace form (verified live) since those header lines
    // already resolve correctly with no capture at all.
    const Mode        php  = ned::editor::PhpMode();
    const std::string text = "<?php\nif ($x):\necho \"a\";\nelseif ($y):\necho \"b\";\nelse:\necho \"c\";\nendif;\n";
    const int         w    = Width(php);
    CHECK(ColumnOf(php, text, 2) == w); // echo "a";
    CHECK(ColumnOf(php, text, 3) == 0); // elseif ($y): -- aligns with "if"
    CHECK(ColumnOf(php, text, 4) == w); // echo "b";
    CHECK(ColumnOf(php, text, 5) == 0); // else: -- aligns with "if"
    CHECK(ColumnOf(php, text, 6) == w); // echo "c";
    CHECK(ColumnOf(php, text, 7) == 0); // endif;

    // The brace form is untouched by the new else_if_clause/else_clause
    // @dedent -- it already had nothing over-indenting it.
    const std::string braceText = "<?php\nif ($x) {\necho \"a\";\n} elseif ($y) {\necho \"b\";\n} else {\necho \"c\";\n}\n";
    CHECK(ColumnOf(php, braceText, 2) == w); // echo "a";
    CHECK(ColumnOf(php, braceText, 3) == 0); // } elseif ($y) {
    CHECK(ColumnOf(php, braceText, 4) == w); // echo "b";
    CHECK(ColumnOf(php, braceText, 5) == 0); // } else {
    CHECK(ColumnOf(php, braceText, 6) == w); // echo "c";
    CHECK(ColumnOf(php, braceText, 7) == 0); // }
}

TEST_CASE("PHP's colon-alternate while/for/foreach indent the same way as if/endif",
          "[Indent][Imprint]") {
    // Same shape and same fix as if_statement's own colon form above --
    // while_statement/for_statement/foreach_statement each close their
    // colon-alternate body on a literal ("endwhile"/"endfor"/"endforeach")
    // belonging to the statement itself, with no elseif/else-shaped
    // alternative to dedent.
    const Mode php = ned::editor::PhpMode();
    const int  w   = Width(php);
    {
        const std::string text = "<?php\nwhile ($x):\necho \"a\";\nendwhile;\n";
        CHECK(ColumnOf(php, text, 2) == w); // echo "a";
        CHECK(ColumnOf(php, text, 3) == 0); // endwhile;
    }
    {
        const std::string text = "<?php\nfor ($i = 0; $i < 10; $i++):\necho \"a\";\nendfor;\n";
        CHECK(ColumnOf(php, text, 2) == w); // echo "a";
        CHECK(ColumnOf(php, text, 3) == 0); // endfor;
    }
    {
        const std::string text = "<?php\nforeach ($xs as $x):\necho \"a\";\nendforeach;\n";
        CHECK(ColumnOf(php, text, 2) == w); // echo "a";
        CHECK(ColumnOf(php, text, 3) == 0); // endforeach;
    }
}

TEST_CASE("PHP's case/default clauses give their own body a further indent level, in both switch forms",
          "[Indent][Imprint]") {
    // ROADMAP.md watch-list entry: case_statement/default_statement carry
    // no delimiters of their own, so their body statements never got a
    // level past their own switch -- unlike Go's exactly analogous
    // clauses (which dedent the LABEL back to switch's level; PHP/PSR-12
    // wants the opposite shape, indenting the label same as any content
    // and the body one level further). Fixed via a plain whole-node
    // @indent on each -- self-excluded from its own "case 1:" line by the
    // walk's usual bracket-opener self-exclusion, with no closer to name
    // since a case's own byte range already ends exactly where the next
    // case/default/closer begins.
    const Mode php = ned::editor::PhpMode();
    const int  w   = Width(php);
    {
        const std::string text = "<?php\nswitch ($x) {\ncase 1:\necho \"a\";\nbreak;\ndefault:\necho \"b\";\n}\n";
        CHECK(ColumnOf(php, text, 2) == w);     // case 1: -- same level as any switch_block content
        CHECK(ColumnOf(php, text, 3) == 2 * w); // echo "a"; -- one further level, inside the case
        CHECK(ColumnOf(php, text, 4) == 2 * w); // break;
        CHECK(ColumnOf(php, text, 5) == w);     // default:
        CHECK(ColumnOf(php, text, 6) == 2 * w); // echo "b";
        CHECK(ColumnOf(php, text, 7) == 0);     // the switch's own closing "}"
    }
    {
        // The colon form of switch was ALSO entirely unindented before this
        // fix (switch_block's brace-vs-colon CHOICE lives inside its own
        // grammar rule, so the pre-existing kPhp[] imprint entry -- brace
        // literals only -- contributes nothing for a colon-form instance).
        const std::string text =
            "<?php\nswitch ($x):\ncase 1:\necho \"a\";\nbreak;\ndefault:\necho \"b\";\nendswitch;\n";
        CHECK(ColumnOf(php, text, 2) == w);     // case 1:
        CHECK(ColumnOf(php, text, 3) == 2 * w); // echo "a";
        CHECK(ColumnOf(php, text, 4) == 2 * w); // break;
        CHECK(ColumnOf(php, text, 5) == w);     // default:
        CHECK(ColumnOf(php, text, 6) == 2 * w); // echo "b";
        CHECK(ColumnOf(php, text, 7) == 0);     // endswitch;
    }
}
