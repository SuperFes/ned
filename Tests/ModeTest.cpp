#include <catch2/catch_test_macros.hpp>

#include "Editor/Key.h"
#include "Editor/Mode.h"
#include "Editor/Org.h"
#include "Editor/SyntaxTheme.h"

using ned::editor::BashMode;
using ned::editor::CMode;
using ned::editor::CppMode;
using ned::editor::CssMode;
using ned::editor::FundamentalMode;
using ned::editor::HighlightSpan;
using ned::editor::HtmlMode;
using ned::editor::JanetMode;
using ned::editor::JavaScriptMode;
using ned::editor::JsonMode;
using ned::editor::Keymap;
using ned::editor::MarkdownMode;
using ned::editor::OrgMode;
using ned::editor::ParseKeySequence;
using ned::editor::PhpMode;
using ned::editor::PythonMode;
using ned::editor::SyntaxClass;
using ned::editor::TsxMode;
using ned::editor::TypeScriptMode;

namespace {

// True if any span in spans covers exactly [start, end) with class cls.
bool HasSpan(const std::vector<HighlightSpan>& spans, std::size_t start, std::size_t end, SyntaxClass cls) {
    for (const HighlightSpan& span : spans) {
        if (span.startByte == start && span.endByte == end && span.syntaxClass == cls) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("FundamentalMode has no keybindings and no highlighting", "[Mode]") {
    const auto mode = FundamentalMode();

    REQUIRE(mode.name == "fundamental-mode");
    REQUIRE_FALSE(static_cast<bool>(mode.highlight));
    REQUIRE(mode.keymap.Resolve(ParseKeySequence("C-c")).result == Keymap::LookupResult::NoMatch);
}

TEST_CASE("JanetMode has a highlighting hook installed", "[Mode]") {
    const auto mode = JanetMode();
    REQUIRE(mode.name == "janet-mode");
    REQUIRE(static_cast<bool>(mode.highlight));
}

TEST_CASE("JanetMode highlights only the numeric literals in plain code, symbols stay Default", "[Mode]") {
    const auto mode  = JanetMode();
    const auto spans = mode.highlight("(+ 1 2)");

    // `+`, the parens, and the spaces are all Default -- no span covers them.
    // Only the two num_lit tokens get a real capture.
    REQUIRE(HasSpan(spans, 3, 4, SyntaxClass::Number)); // `1`
    REQUIRE(HasSpan(spans, 5, 6, SyntaxClass::Number)); // `2`
    REQUIRE(spans.size() == 2);
}

TEST_CASE("JanetMode highlights a full-line comment as entirely Comment", "[Mode]") {
    const auto mode  = JanetMode();
    const auto spans = mode.highlight("# this is a comment");

    REQUIRE(HasSpan(spans, 0, 19, SyntaxClass::Comment));
}

TEST_CASE("JanetMode highlights a string literal, code around it stays Default", "[Mode]") {
    const auto             mode  = JanetMode();
    const std::string_view line  = R"((print "hi"))";
    const auto             spans = mode.highlight(line);

    // (print␣  -> Default (7 bytes: '(', p, r, i, n, t, ' ') -- no span
    // "hi" -> String (4 bytes: '"', h, i, '"'), starting at byte 7
    REQUIRE(HasSpan(spans, 7, 11, SyntaxClass::String));
    // trailing ')' at byte 11 stays Default -- no span covers it
    for (const HighlightSpan& span : spans) {
        const bool coversTrailingParen = (span.startByte <= 11 && 11 < span.endByte);
        REQUIRE_FALSE(coversTrailingParen);
    }
}

TEST_CASE("JanetMode treats a backslash-escaped quote as staying inside the string", "[Mode]") {
    const auto             mode  = JanetMode();
    const std::string_view line  = R"("a\"b")"; // "a\"b" -- 6 bytes: " a \ " b "
    const auto             spans = mode.highlight(line);

    REQUIRE(HasSpan(spans, 0, line.size(), SyntaxClass::String)); // the whole thing is one string literal
}

TEST_CASE("JanetMode switches from string to comment correctly on the same line", "[Mode]") {
    const auto             mode  = JanetMode();
    const std::string_view line  = R"("str" # comment)";
    const auto             spans = mode.highlight(line);

    REQUIRE(HasSpan(spans, 0, 5, SyntaxClass::String));   // `"str"`
    REQUIRE(HasSpan(spans, 6, 15, SyntaxClass::Comment)); // `# comment`
}

TEST_CASE("JanetMode's spans use byte offsets, correctly spanning a multi-byte codepoint", "[Mode]") {
    const auto mode  = JanetMode();
    const auto spans = mode.highlight("# caf\xC3\xA9"); // "# café" -- 'é' is 2 bytes

    REQUIRE(HasSpan(spans, 0, 7, SyntaxClass::Comment)); // 7 bytes total, not 6 codepoints
}

TEST_CASE("JanetMode highlights a long string literal as one continuous span across a newline", "[Mode]") {
    // The real grammar sees the whole buffer at once, unlike the old
    // hand-rolled per-line scanner it replaced (which reset its state at
    // every '\n' and fundamentally could not represent a construct spanning
    // multiple lines -- see HighlightSpan's own doc comment). A Janet
    // backtick-delimited long string is exactly that construct.
    const auto             mode  = JanetMode();
    const std::string_view text  = "`line1\nline2`";
    const auto             spans = mode.highlight(text);

    REQUIRE(HasSpan(spans, 0, text.size(), SyntaxClass::String));
}

TEST_CASE("JanetMode does not classify an unterminated string literal", "[Mode]") {
    const auto mode  = JanetMode();
    const auto spans = mode.highlight("\"unterminated\nplain code");

    REQUIRE(spans.empty()); // no valid str_lit node for the parser to capture
}

TEST_CASE("JsonMode has a highlighting hook installed", "[Mode]") {
    const auto mode = JsonMode();
    REQUIRE(mode.name == "json-mode");
    REQUIRE(static_cast<bool>(mode.highlight));
}

TEST_CASE("JsonMode highlights strings, numbers, and literal keywords via a real tree-sitter parse", "[Mode]") {
    const auto             mode  = JsonMode();
    const std::string_view text  = R"({"a": 1, "b": true, "c": null})";
    const auto             spans = mode.highlight(text);

    REQUIRE(HasSpan(spans, 1, 4, SyntaxClass::String));            // "a"
    REQUIRE(HasSpan(spans, 6, 7, SyntaxClass::Number));            // 1
    REQUIRE(HasSpan(spans, 9, 12, SyntaxClass::String));           // "b"
    REQUIRE(HasSpan(spans, 14, 18, SyntaxClass::ConstantBuiltin)); // true
    REQUIRE(HasSpan(spans, 20, 23, SyntaxClass::String));          // "c"
    REQUIRE(HasSpan(spans, 25, 29, SyntaxClass::ConstantBuiltin)); // null
}

TEST_CASE("Highlight spans carry the interned capture id of the producing capture", "[Mode]") {
    const auto             mode  = JsonMode();
    const std::string_view text  = R"({"a": 1})";
    const auto             spans = mode.highlight(text);

    // The number 1 is tree-sitter-json's own "@number" capture -- its span
    // must carry a real capture id resolving back to that name, the
    // exhaustive-highlighting follow-up's whole point (per-capture styling
    // needs to know *which capture*, not just the resolved class).
    bool found = false;
    for (const ned::editor::HighlightSpan& span : spans) {
        if (span.startByte == 6 && span.endByte == 7) {
            REQUIRE(span.captureId != ned::editor::kNoCapture);
            REQUIRE(ned::editor::CaptureNameForId(span.captureId) == "number");
            found = true;
        }
    }
    REQUIRE(found);
}

TEST_CASE("An equal-range double capture keeps the more specific capture name, regardless of pattern order", "[Mode]") {
    // tree-sitter-json's own query captures a key node twice -- '(pair key:
    // (_) @string.special.key)' then '(string) @string' -- with the exact
    // same byte range. Found live (a smoke test's ned/set-capture-foreground
    // on "string.special.key" silently did nothing): the later generic
    // capture used to clobber the specific one's captureId under the
    // later-span-wins render rule. SpanCollector (Mode.cpp) resolves the
    // tie by specificity now.
    const auto             mode  = JsonMode();
    const std::string_view text  = R"({"a": 1})";
    const auto             spans = mode.highlight(text);

    bool found = false;
    for (const ned::editor::HighlightSpan& span : spans) {
        if (span.startByte == 1 && span.endByte == 4) {
            REQUIRE(ned::editor::CaptureNameForId(span.captureId) == "string.special.key");
            found = true;
        }
    }
    REQUIRE(found);
}

TEST_CASE("A ned/set-capture-class remap re-bases what a capture resolves to at parse time", "[Mode]") {
    struct RemapGuard {
        ~RemapGuard() {
            ned::editor::SetSyntaxClassForCapture("number", std::nullopt);
        }
    } guard;
    ned::editor::SetSyntaxClassForCapture("number", SyntaxClass::Comment);

    const auto spans = JsonMode().highlight("[1]");
    REQUIRE(HasSpan(spans, 1, 2, SyntaxClass::Comment)); // "1" -- remapped away from Number
}

// generic-tree-sitter-highlighting follow-up: CMode/CppMode now use a real,
// rich query (Source/Editor/TreeSitter/queries/c.scm, cpp.scm) vendored
// from nvim-treesitter -- these cases exercise the specific distinctions
// that motivated vendoring it (access specifiers, header-vs-quoted
// includes, return types, parameters, method-vs-free-function calls), not
// just "some highlighting exists." Byte offsets computed against the real
// grammar via `tree-sitter parse`/`tree-sitter query`, not guessed -- the
// same verification method every other Mode test in this file already
// uses.

TEST_CASE("CppMode gives an access specifier its own KeywordModifier class, distinct from a plain keyword",
          "[Mode]") {
    const auto             mode  = CppMode();
    const std::string_view text  = "class Widget {\npublic:\n    int getValue() const { return value_; }\nprivate:\n"
                                   "    int value_ = 0;\n};\n";
    const auto             spans = mode.highlight(text);

    REQUIRE(HasSpan(spans, 15, 21, SyntaxClass::KeywordModifier)); // "public"
    REQUIRE(HasSpan(spans, 67, 74, SyntaxClass::KeywordModifier)); // "private"
    REQUIRE(HasSpan(spans, 27, 30, SyntaxClass::ReturnType));      // "int" (getValue's own return type)
    REQUIRE(HasSpan(spans, 31, 39, SyntaxClass::Method));          // "getValue"
}

TEST_CASE("CppMode splits a \"<system>\" include from a \"\\\"local\\\"\" one, even though both parsed as "
          "plain strings before",
          "[Mode]") {
    const auto             mode  = CppMode();
    const std::string_view text  = "#include <vector>\n#include \"local.h\"\n";
    const auto             spans = mode.highlight(text);

    REQUIRE(HasSpan(spans, 9, 17, SyntaxClass::IncludePath)); // <vector>
    REQUIRE(HasSpan(spans, 27, 36, SyntaxClass::String));     // "local.h" -- plain string, not IncludePath
    for (const HighlightSpan& span : spans) {
        // The quoted form must never end up classified as an include path --
        // that would defeat the whole point of the split.
        REQUIRE_FALSE((span.startByte == 27 && span.endByte == 36 && span.syntaxClass == SyntaxClass::IncludePath));
    }
}

TEST_CASE("CppMode gives a function's parameters Parameter and a return type ReturnType, not the same "
          "generic Type",
          "[Mode]") {
    const auto             mode  = CppMode();
    const std::string_view text  = "int add(int a, int b) { return a + b; }";
    const auto             spans = mode.highlight(text);

    REQUIRE(HasSpan(spans, 0, 3, SyntaxClass::ReturnType));  // "int" (add's own return type)
    REQUIRE(HasSpan(spans, 12, 13, SyntaxClass::Parameter)); // "a"
    REQUIRE(HasSpan(spans, 19, 20, SyntaxClass::Parameter)); // "b"
}

TEST_CASE("CppMode distinguishes a method call from a free function call", "[Mode]") {
    const auto             mode = CppMode();
    const std::string_view text =
        "struct S { void go() {} };\nvoid free_func() {}\nint main() { S s; s.go(); free_func(); }\n";
    const auto spans = mode.highlight(text);

    REQUIRE(HasSpan(spans, 67, 69, SyntaxClass::Method));   // s.go()'s own "go"
    REQUIRE(HasSpan(spans, 73, 82, SyntaxClass::Function)); // free_func() -- a plain free function call
}

TEST_CASE("CMode also gets the richer query -- an access specifier isn't valid C, but return types and "
          "parameters still are",
          "[Mode]") {
    const auto             mode  = CMode();
    const std::string_view text  = "int add(int a, int b) { return a + b; }";
    const auto             spans = mode.highlight(text);

    REQUIRE(HasSpan(spans, 0, 3, SyntaxClass::ReturnType));
    REQUIRE(HasSpan(spans, 12, 13, SyntaxClass::Parameter));
}

TEST_CASE("JsonMode highlights nothing for a JSON value with no strings/numbers/literals", "[Mode]") {
    const auto mode  = JsonMode();
    const auto spans = mode.highlight("{}");

    REQUIRE(spans.empty());
}

TEST_CASE("OrgMode has a highlighting hook installed", "[Mode]") {
    const auto mode = OrgMode();
    REQUIRE(mode.name == "org-mode");
    REQUIRE(static_cast<bool>(mode.highlight));
}

TEST_CASE("OrgMode cycles headline levels every 3 stars, coloring the whole line", "[Mode]") {
    const auto mode  = OrgMode();
    const auto spans = mode.highlight("* L1\n** L2\n*** L3\n**** L4\n");

    REQUIRE(HasSpan(spans, 0, 4, SyntaxClass::HeadlineLevel1));
    REQUIRE(HasSpan(spans, 5, 10, SyntaxClass::HeadlineLevel2));
    REQUIRE(HasSpan(spans, 11, 17, SyntaxClass::HeadlineLevel3));
    REQUIRE(HasSpan(spans, 18, 25, SyntaxClass::HeadlineLevel1)); // wraps back to level 1
}

TEST_CASE("OrgMode colors a default-keyword TODO/DONE distinctly, DONE as the last-configured state",
          "[Mode]") {
    const auto mode  = OrgMode();
    const auto spans = mode.highlight("* TODO Buy milk\n* DONE Clean\n");

    REQUIRE(HasSpan(spans, 2, 6, SyntaxClass::TodoKeyword));
    REQUIRE(HasSpan(spans, 18, 22, SyntaxClass::DoneKeyword));
}

TEST_CASE("OrgMode gives no keyword span when a headline's first word isn't a configured keyword", "[Mode]") {
    const auto mode  = OrgMode();
    const auto spans = mode.highlight("* Buy eggs\n");

    for (const HighlightSpan& span : spans) {
        REQUIRE(span.syntaxClass != SyntaxClass::TodoKeyword);
        REQUIRE(span.syntaxClass != SyntaxClass::DoneKeyword);
    }
}

TEST_CASE("OrgMode respects a custom configured keyword list, not hardcoded TODO/DONE text", "[Mode]") {
    struct TodoKeywordsGuard {
        std::vector<std::string> previous = ned::editor::org::TodoKeywords();
        ~TodoKeywordsGuard() {
            ned::editor::org::SetTodoKeywords(previous);
        }
    } guard;
    ned::editor::org::SetTodoKeywords({"TODO", "IN-PROGRESS", "DONE"});

    const auto mode  = OrgMode();
    const auto spans = mode.highlight("* IN-PROGRESS Ship it\n");

    // The middle keyword in a 3-item list is still "open," not "done" --
    // only the LAST configured keyword is DoneKeyword.
    REQUIRE(HasSpan(spans, 2, 13, SyntaxClass::TodoKeyword));
}

TEST_CASE("OrgMode highlights tags, checkboxes, and comments", "[Mode]") {
    const auto mode = OrgMode();

    const auto tagSpans = mode.highlight("* TODO Buy milk :tag1:tag2:\n");
    REQUIRE(HasSpan(tagSpans, 17, 21, SyntaxClass::Tag));
    REQUIRE(HasSpan(tagSpans, 22, 26, SyntaxClass::Tag));

    const auto checkboxSpans = mode.highlight("- [X] done item\n");
    REQUIRE(HasSpan(checkboxSpans, 2, 5, SyntaxClass::Checkbox));

    const auto commentSpans = mode.highlight("# a comment\n");
    REQUIRE(HasSpan(commentSpans, 0, 12, SyntaxClass::Comment)); // spans through its own trailing newline
}

TEST_CASE("OrgMode highlights a directive name and a block's begin/end names", "[Mode]") {
    const auto mode = OrgMode();

    const auto directiveSpans = mode.highlight("#+TITLE: My Title\n");
    REQUIRE(HasSpan(directiveSpans, 2, 7, SyntaxClass::Keyword));

    const auto blockSpans = mode.highlight("#+begin_src python\ncode\n#+end_src\n");
    REQUIRE(HasSpan(blockSpans, 8, 11, SyntaxClass::Keyword));  // "src" (begin name)
    REQUIRE(HasSpan(blockSpans, 30, 33, SyntaxClass::Keyword)); // "src" (end name)
}

TEST_CASE("OrgMode highlights a table's horizontal rule and a timestamp", "[Mode]") {
    const auto mode = OrgMode();

    const auto tableSpans = mode.highlight("| a | b |\n|---+---|\n| c | d |\n");
    REQUIRE(HasSpan(tableSpans, 10, 20, SyntaxClass::Punctuation)); // spans through its own trailing newline

    // A bare timestamp isn't its own construct anywhere in body text --
    // only ever real inside a headline's own "plan" line (e.g. a
    // SCHEDULED:/DEADLINE: entry), confirmed via a real parse before
    // writing this test, not assumed.
    const auto timestampSpans = mode.highlight("* Heading\nSCHEDULED: <2024-01-01 Mon>\n");
    REQUIRE(HasSpan(timestampSpans, 21, 37, SyntaxClass::Constant));
}

TEST_CASE("OrgMode highlights all six inline emphasis markers as real spans", "[Mode]") {
    const auto mode = OrgMode();
    const auto spans =
        mode.highlight("This is *bold* and /italic/ and _underline_ and =verbatim= and ~code~ and +strike+.\n");

    REQUIRE(HasSpan(spans, 8, 14, SyntaxClass::Strong));
    REQUIRE(HasSpan(spans, 19, 27, SyntaxClass::Emphasis));
    REQUIRE(HasSpan(spans, 32, 43, SyntaxClass::Underline));
    REQUIRE(HasSpan(spans, 48, 58, SyntaxClass::String));
    REQUIRE(HasSpan(spans, 63, 69, SyntaxClass::String));
    REQUIRE(HasSpan(spans, 74, 82, SyntaxClass::Strikethrough));
}

TEST_CASE("OrgMode never opens emphasis mid-word", "[Mode]") {
    const auto mode  = OrgMode();
    const auto spans = mode.highlight("2*3 is six\n");

    for (const HighlightSpan& span : spans) {
        REQUIRE(span.syntaxClass != SyntaxClass::Strong);
    }
}

TEST_CASE("OrgMode doesn't parse markup inside verbatim/code as nested emphasis", "[Mode]") {
    const auto mode = OrgMode();

    const auto verbatimSpans = mode.highlight("=a /b/ c=\n");
    REQUIRE(HasSpan(verbatimSpans, 0, 9, SyntaxClass::String));
    for (const HighlightSpan& span : verbatimSpans) {
        REQUIRE(span.syntaxClass != SyntaxClass::Emphasis);
    }
}

TEST_CASE("OrgMode highlights nested emphasis of different markers", "[Mode]") {
    const auto mode  = OrgMode();
    const auto spans = mode.highlight("*bold /italic/*\n");

    REQUIRE(HasSpan(spans, 0, 15, SyntaxClass::Strong));
    REQUIRE(HasSpan(spans, 6, 14, SyntaxClass::Emphasis));
}

// generic-code-folding follow-up: every in-scope mode has a real fold
// query; every out-of-scope one stays exactly as it was (empty .fold) --
// this is the "no gutter affordance when we can't fold" contract, tested
// directly at the Mode level rather than only via BufferView's gutter.
TEST_CASE("In-scope modes have a fold hook installed", "[Mode]") {
    REQUIRE(static_cast<bool>(CMode().fold));
    REQUIRE(static_cast<bool>(CppMode().fold));
    REQUIRE(static_cast<bool>(JsonMode().fold));
    REQUIRE(static_cast<bool>(ned::editor::PythonMode().fold));
    REQUIRE(static_cast<bool>(ned::editor::JavaScriptMode().fold));
    REQUIRE(static_cast<bool>(ned::editor::TypeScriptMode().fold));
    REQUIRE(static_cast<bool>(ned::editor::TsxMode().fold));
    REQUIRE(static_cast<bool>(ned::editor::ClojureMode().fold));
    REQUIRE(static_cast<bool>(ned::editor::JankMode().fold));
}

TEST_CASE("Out-of-scope and non-tree-sitter modes have no fold hook", "[Mode]") {
    REQUIRE_FALSE(static_cast<bool>(FundamentalMode().fold));
    REQUIRE_FALSE(static_cast<bool>(JanetMode().fold));
    REQUIRE_FALSE(static_cast<bool>(OrgMode().fold));
    REQUIRE_FALSE(static_cast<bool>(ned::editor::PhpMode().fold));
    REQUIRE_FALSE(static_cast<bool>(ned::editor::HtmlMode().fold));
    REQUIRE_FALSE(static_cast<bool>(ned::editor::CssMode().fold));
    REQUIRE_FALSE(static_cast<bool>(ned::editor::BashMode().fold));
    REQUIRE_FALSE(static_cast<bool>(ned::editor::MarkdownMode().fold));
    REQUIRE_FALSE(static_cast<bool>(ned::editor::YamlMode().fold));
    REQUIRE_FALSE(static_cast<bool>(ned::editor::TomlMode().fold));
}

TEST_CASE("CMode's fold query finds a function body", "[Mode]") {
    const auto mode   = CMode();
    const auto blocks = mode.fold("int main(void) {\n    return 0;\n}\n");
    REQUIRE(blocks.size() == 1);
}

// Regression test for a real, reported bug: c.scm/cpp.scm (vendored from
// nvim-treesitter) tag a comment node with BOTH "@comment" and "@spell" in
// one pattern -- "@spell" is a Neovim-specific spell-check hint, not a real
// highlight class, and has no CaptureTable() entry, so it used to resolve
// to SyntaxClass::Default and then silently clobber the real "@comment"
// span via HighlightSpan's own "later capture wins" overlap rule -- comments
// rendered with no color/italic at all, no matter what Theme said, since
// the actual applied class was always Default. See IsHighlightableCapture's
// own doc comment in Mode.cpp for the full story and the general fix
// (filtering nvim-treesitter's underscore-prefixed/"spell" convention
// before a HighlightSpan is ever created, not just for comments).
TEST_CASE("CMode classifies a comment as Comment, not clobbered to Default by @spell", "[Mode]") {
    const auto mode  = CMode();
    const auto spans = mode.highlight("// a comment\nint x;\n");
    REQUIRE(HasSpan(spans, 0, 12, SyntaxClass::Comment));
    for (const HighlightSpan& span : spans) {
        if (span.startByte == 0 && span.endByte == 12) {
            REQUIRE(span.syntaxClass == SyntaxClass::Comment); // never Default
        }
    }
}

TEST_CASE("CppMode classifies a comment as Comment, not clobbered to Default by @spell", "[Mode]") {
    const auto mode  = CppMode();
    const auto spans = mode.highlight("// a comment\nint x;\n");
    REQUIRE(HasSpan(spans, 0, 12, SyntaxClass::Comment));
}

TEST_CASE("YamlMode has a highlighting hook installed and classifies a comment as Comment", "[Mode]") {
    const auto mode = ned::editor::YamlMode();
    REQUIRE(mode.name == "yaml-mode");
    REQUIRE(static_cast<bool>(mode.highlight));

    const auto spans = mode.highlight("# a comment\nkey: value\n");
    REQUIRE(HasSpan(spans, 0, 11, SyntaxClass::Comment));
}

TEST_CASE("TomlMode has a highlighting hook installed and classifies a string as String", "[Mode]") {
    const auto mode = ned::editor::TomlMode();
    REQUIRE(mode.name == "toml-mode");
    REQUIRE(static_cast<bool>(mode.highlight));

    const auto spans = mode.highlight("key = \"value\"\n");
    REQUIRE(HasSpan(spans, 6, 13, SyntaxClass::String));
}

TEST_CASE("ClojureMode has a highlighting hook installed and classifies core constructs", "[Mode]") {
    const auto mode = ned::editor::ClojureMode();
    REQUIRE(mode.name == "clojure-mode");
    REQUIRE(static_cast<bool>(mode.highlight));

    const auto spans = mode.highlight("; a comment\n(defn greet [name]\n  (str \"hi \" name))\n");
    // [0,12), not [0,11): sogaiu's COMMENT token regex is `(;|#!).*\n?` --
    // the trailing newline is part of the comment node, unlike C/YAML/....
    REQUIRE(HasSpan(spans, 0, 12, SyntaxClass::Comment));
    REQUIRE(HasSpan(spans, 13, 17, SyntaxClass::Keyword)); // `defn` -> @keyword.function
    REQUIRE(HasSpan(spans, 38, 43, SyntaxClass::String));  // `"hi "`
}

// JankMode is the same grammar/query as ClojureMode under its own name (jank
// is a Clojure dialect with no tree-sitter grammar of its own -- see Mode.h)
// -- assert both halves of that: distinct identity, identical classification.
TEST_CASE("JankMode shares ClojureMode's grammar and query under its own name", "[Mode]") {
    const auto mode = ned::editor::JankMode();
    REQUIRE(mode.name == "jank-mode");
    REQUIRE(static_cast<bool>(mode.highlight));

    const auto spans = mode.highlight("(defn add [a b]\n  (cpp/+ a b))\n");
    REQUIRE(HasSpan(spans, 1, 5, SyntaxClass::Keyword)); // `defn`, same query as clojure-mode
}

TEST_CASE("ClojureMode's fold query finds a top-level form's body", "[Mode]") {
    const auto mode   = ned::editor::ClojureMode();
    const auto blocks = mode.fold("(defn f\n  [x]\n  x)\n");
    REQUIRE_FALSE(blocks.empty());
}

TEST_CASE("Each *Mode() sets lineCommentPrefix to its language's real line-comment token, or leaves it empty", "[Mode]") {
    REQUIRE(CMode().lineCommentPrefix == "//");
    REQUIRE(CppMode().lineCommentPrefix == "//");
    REQUIRE(PhpMode().lineCommentPrefix == "//");
    REQUIRE(JavaScriptMode().lineCommentPrefix == "//");
    REQUIRE(TypeScriptMode().lineCommentPrefix == "//");
    REQUIRE(TsxMode().lineCommentPrefix == "//");
    REQUIRE(PythonMode().lineCommentPrefix == "#");
    REQUIRE(BashMode().lineCommentPrefix == "#");
    REQUIRE(ned::editor::YamlMode().lineCommentPrefix == "#");
    REQUIRE(ned::editor::TomlMode().lineCommentPrefix == "#");
    REQUIRE(JanetMode().lineCommentPrefix == ";");
    REQUIRE(ned::editor::ClojureMode().lineCommentPrefix == ";");
    REQUIRE(ned::editor::JankMode().lineCommentPrefix == ";");
    REQUIRE(OrgMode().lineCommentPrefix == "#");
    // No native single-line-comment token to toggle.
    REQUIRE(JsonMode().lineCommentPrefix.empty());
    REQUIRE(HtmlMode().lineCommentPrefix.empty());
    REQUIRE(CssMode().lineCommentPrefix.empty());
    REQUIRE(MarkdownMode().lineCommentPrefix.empty());
    REQUIRE(FundamentalMode().lineCommentPrefix.empty());
}

TEST_CASE("MarkdownMode and OrgMode default to wrapLines true; every other bundled mode defaults to false",
          "[Mode]") {
    REQUIRE(MarkdownMode().wrapLines);
    REQUIRE(OrgMode().wrapLines);

    REQUIRE_FALSE(FundamentalMode().wrapLines);
    REQUIRE_FALSE(JanetMode().wrapLines);
    REQUIRE_FALSE(JsonMode().wrapLines);
    REQUIRE_FALSE(CMode().wrapLines);
    REQUIRE_FALSE(CppMode().wrapLines);
    REQUIRE_FALSE(PhpMode().wrapLines);
    REQUIRE_FALSE(JavaScriptMode().wrapLines);
    REQUIRE_FALSE(TypeScriptMode().wrapLines);
    REQUIRE_FALSE(TsxMode().wrapLines);
    REQUIRE_FALSE(HtmlMode().wrapLines);
    REQUIRE_FALSE(CssMode().wrapLines);
    REQUIRE_FALSE(PythonMode().wrapLines);
    REQUIRE_FALSE(BashMode().wrapLines);
    REQUIRE_FALSE(ned::editor::YamlMode().wrapLines);
    REQUIRE_FALSE(ned::editor::TomlMode().wrapLines);
    REQUIRE_FALSE(ned::editor::ClojureMode().wrapLines);
    REQUIRE_FALSE(ned::editor::JankMode().wrapLines);
}

// structural-selection-expansion follow-up: every TreeSitterModeFromLanguage-
// built mode gets a real expandSelection hook for free; FundamentalMode (no
// parser at all) and OrgMode (its own separate, non-shared highlight
// closure -- see Mode.cpp's own comment) are the documented v1 scope cut.
TEST_CASE("Tree-sitter-backed modes have an expandSelection hook installed; Fundamental/Org don't", "[Mode]") {
    REQUIRE(static_cast<bool>(CMode().expandSelection));
    REQUIRE(static_cast<bool>(JsonMode().expandSelection));
    REQUIRE(static_cast<bool>(ned::editor::PythonMode().expandSelection));

    REQUIRE_FALSE(static_cast<bool>(FundamentalMode().expandSelection));
    REQUIRE_FALSE(static_cast<bool>(OrgMode().expandSelection));
}

TEST_CASE("JsonMode's expandSelection grows step by step from a point and terminates at the root", "[Mode]") {
    const auto        mode = JsonMode();
    const std::string text = R"({"a": 1})";

    std::optional<std::pair<std::size_t, std::size_t>> range = std::make_pair(std::size_t{6}, std::size_t{6}); // inside the "1"
    std::vector<std::string>                           steps;
    for (int i = 0; i < 10 && range.has_value(); ++i) {
        range = mode.expandSelection(text, range->first, range->second);
        if (range.has_value()) {
            steps.push_back(text.substr(range->first, range->second - range->first));
        }
    }

    REQUIRE_FALSE(range.has_value()); // eventually runs out of enclosing nodes
    REQUIRE_FALSE(steps.empty());
    REQUIRE(steps.front() == "1");
    REQUIRE(steps.back() == text);
    for (std::size_t i = 1; i < steps.size(); ++i) {
        REQUIRE(steps[i].size() >= steps[i - 1].size()); // never shrinks or repeats a step
    }
}

TEST_CASE("JsonMode's expandSelection grows an existing selection to its next enclosing node", "[Mode]") {
    const auto        mode = JsonMode();
    const std::string text = R"({"a": 1})";

    // The "1" number literal is already selected -- expanding once more
    // should grow to the enclosing "a": 1 pair, not just re-select "1".
    const std::optional<std::pair<std::size_t, std::size_t>> numberRange = mode.expandSelection(text, 6, 6);
    REQUIRE(numberRange.has_value());
    REQUIRE(text.substr(numberRange->first, numberRange->second - numberRange->first) == "1");

    const std::optional<std::pair<std::size_t, std::size_t>> pairRange = mode.expandSelection(text, numberRange->first, numberRange->second);
    REQUIRE(pairRange.has_value());
    REQUIRE(text.substr(pairRange->first, pairRange->second - pairRange->first) == "\"a\": 1");
}
