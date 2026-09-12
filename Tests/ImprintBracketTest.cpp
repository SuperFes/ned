#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>

#include "Editor/Command.h"
#include "Editor/Commands.h"
#include "Editor/ImprintBracket.h"
#include "Editor/Mode.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "Text/KillRing.h"
#include "Editor/TreeSitter/Languages.h"
#include "Editor/TreeSitter/Parser.h"
#include "Editor/TreeSitter/Tree.h"

using ned::editor::imprint::MatchingDelimiterOffset;
using ned::editor::imprint::MatchingDelimitersAt;

namespace {

struct Parsed {
    ned::editor::treesitter::Parser parser;
    ned::editor::treesitter::Tree   tree;
};

// Held by value so the Tree outlives every Node taken from it.
std::optional<std::size_t> Match(const std::string& language, const std::string& text, std::size_t point) {
    const auto resolved = ned::editor::treesitter::LanguageByName(language);
    REQUIRE(resolved.has_value());
    const ned::editor::treesitter::Parser parser(*resolved);
    const ned::editor::treesitter::Tree   tree = parser.Parse(text);
    return MatchingDelimiterOffset(tree.RootNode(), language, point);
}

} // namespace

TEST_CASE("A caret on a brace finds its partner, both directions", "[ImprintBracket]") {
    //                     0123456789...
    const std::string text = "int f(void) {\n    return 1;\n}\n";
    const std::size_t open  = text.find('{');
    const std::size_t close = text.rfind('}');

    CHECK(Match("c", text, open) == close);
    CHECK(Match("c", text, close) == open);
}

TEST_CASE("A caret just past a delimiter still matches it", "[ImprintBracket]") {
    // With the caret immediately after a closing brace, that brace is the one
    // you meant -- every editor treats it that way.
    const std::string text  = "int f(void) {\n    return 1;\n}\n";
    const std::size_t open  = text.find('{');
    const std::size_t close = text.rfind('}');

    CHECK(Match("c", text, close + 1) == open);
    CHECK(Match("c", text, open + 1) == close);
}

TEST_CASE("The innermost pair wins over an enclosing one", "[ImprintBracket]") {
    const std::string text      = "int f(void) {\n    if (x) {\n        g();\n    }\n}\n";
    const std::size_t innerOpen = text.find('{', text.find("if"));
    const std::size_t innerClose = text.find('}');

    CHECK(Match("c", text, innerOpen) == innerClose);
    CHECK(Match("c", text, innerClose) == innerOpen);
}

TEST_CASE("A brace inside a string or comment is not a delimiter", "[ImprintBracket]") {
    // The reason to do this on the parse tree rather than by counting
    // characters: a scanner has to be told about strings and comments, and
    // gets it wrong per language. The parse already settled it.
    const std::string text  = "int f(void) {\n    const char* s = \"} not a brace\";\n}\n";
    const std::size_t inString = text.find('}');
    const std::size_t realClose = text.rfind('}');
    const std::size_t open      = text.find('{');

    CHECK(Match("c", text, inString) == std::nullopt);
    CHECK(Match("c", text, open) == realClose);
}

TEST_CASE("The delimiters need not be the node's first and last children", "[ImprintBracket]") {
    // `a[0]` parses as `identifier` `[` `number_literal` `]`, so reading child
    // 0 as the opener reported the identifier and matching on `[` failed
    // outright -- every `openerIsFirst == false` body was in that position.
    const std::string text  = "int f(void) {\n    int a[3];\n    return a[0];\n}\n";
    const std::size_t open  = text.find("a[0]") + 1;
    const std::size_t close = text.find("a[0]") + 3;

    CHECK(Match("c", text, open) == close);
    CHECK(Match("c", text, close) == open);
}

TEST_CASE("A node whose production can be unbracketed reports no pair when it is", "[ImprintBracket]") {
    // Kotlin's `function_body` is `{ ... }` or `= expr`. The table says the
    // TYPE is delimited, which is true of one alternative; this instance is
    // the other one.
    const std::string expr = "fun double(n: Int): Int = n * 2\n";
    CHECK(Match("kotlin", expr, expr.find('=')) == std::nullopt);

    const std::string braced = "fun twice(n: Int): Int {\n    return n * 2\n}\n";
    CHECK(Match("kotlin", braced, braced.find('{')) == braced.rfind('}'));
}

TEST_CASE("An unclosed brace still pairs, via the parser's own missing token", "[ImprintBracket]") {
    // Requiring a real closer would break folding and matching mid-typing if
    // error recovery dropped it. It does not: tree-sitter inserts a zero-width
    // MISSING `}` at the end, which is found the same way a written one is.
    // The missing token sits where the closer would have gone -- directly
    // after the last statement, not at end of file.
    const std::string text = "int f(void) {\n    int x = 1;\n";
    CHECK(Match("c", text, text.find('{')) == text.rfind(';') + 1);
}

TEST_CASE("Point away from any delimiter matches nothing", "[ImprintBracket]") {
    const std::string text = "int f(void) {\n    return 1;\n}\n";
    CHECK(Match("c", text, text.find("return")) == std::nullopt);
}

TEST_CASE("Bracket matching works for every bracket kind and several languages", "[ImprintBracket]") {
    const std::string rust = "fn f() {\n    let xs = [\n        1,\n        2,\n    ];\n}\n";
    CHECK(Match("rust", rust, rust.find('[')) == rust.find(']'));

    const std::string json = "{\n  \"a\": [\n    1\n  ]\n}\n";
    CHECK(Match("json", json, json.find('[')) == json.find(']'));
    CHECK(Match("json", json, json.find('{')) == json.rfind('}'));

    const std::string php = "<?php\nfunction f() {\n    return 1;\n}\n";
    CHECK(Match("php", php, php.find('{')) == php.rfind('}'));
}

TEST_CASE("An indentation body reports no bracket, which is the honest answer", "[ImprintBracket]") {
    // Python's `block` is delimited by a dedent, not by a pair of characters.
    // Reporting that as a bracket match would be a lie told to a feature whose
    // entire job is precision about which character pairs with which.
    // No parentheses anywhere near the colon -- a first attempt used
    // `def f():` and the colon there sits immediately after `)`, so the
    // just-past-a-delimiter rule correctly matched the parens and the test was
    // measuring the wrong thing.
    const std::string python = "if x:\n    pass\n";
    CHECK(Match("python", python, python.find(':')) == std::nullopt);

    // Parentheses still match, because those genuinely are a pair.
    const std::string call = "f(1)\n";
    CHECK(Match("python", call, call.find('(')) == call.find(')'));
}

TEST_CASE("A language with no compiled-in table answers nothing rather than guessing", "[ImprintBracket]") {
    const std::string org = "* A headline\n";
    CHECK(Match("org", org, 0) == std::nullopt);
}

TEST_CASE("goto-matching-bracket moves point to the partner", "[ImprintBracket][Commands]") {
    // Through the real command registry, since a capability nobody can invoke
    // is not a feature.
    ned::editor::CommandRegistry registry;
    ned::editor::RegisterBuiltinCommands(registry);

    ned::text::Buffer     buffer("t.c");
    ned::text::KillRing   killRing;
    ned::text::BufferList bufferList;
    buffer.InsertAtPoint("int f(void) {\n    return 1;\n}\n");

    const std::size_t open  = buffer.Text().find('{');
    const std::size_t close = buffer.Text().rfind('}');

    ned::editor::Mode mode = ned::editor::CMode();
    std::string       message;

    auto invoke = [&](std::size_t from) {
        buffer.SetPoint(from);
        ned::editor::CommandContext context{buffer, killRing, bufferList};
        context.mode    = &mode;
        context.message = &message;
        registry.Invoke("goto-matching-bracket", context);
        return buffer.Point();
    };

    CHECK(invoke(open) == close);
    CHECK(invoke(close) == open);

    // Off a bracket: point stays put and the user is told why.
    message.clear();
    CHECK(invoke(buffer.Text().find("return")) == buffer.Text().find("return"));
    CHECK_FALSE(message.empty());
}

TEST_CASE("The Mode capability is present exactly where brackets mean something", "[ImprintBracket]") {
    // Present for a language whose delimiters are bracket pairs, absent
    // otherwise -- and absent is a real answer, not a gap. A mode with no
    // compiled-in imprint (Org folds by headline depth) has no brackets to
    // match, and saying so lets goto-matching-bracket report that rather than
    // silently doing nothing.
    CHECK(static_cast<bool>(ned::editor::CMode().matchingDelimiters));
    CHECK(static_cast<bool>(ned::editor::PhpMode().matchingDelimiters));
    CHECK(static_cast<bool>(ned::editor::YamlMode().matchingDelimiters));
    // jank shares Clojure's grammar but keys its own table by mode name, and
    // had no entry until the fold queries were deleted and JankMode stopped
    // folding. It had been missing bracket matching that whole time, silently,
    // because nothing else consulted the table. Pinned so it cannot recur.
    CHECK(static_cast<bool>(ned::editor::ClojureMode().matchingDelimiters));
    CHECK(static_cast<bool>(ned::editor::JankMode().matchingDelimiters));

    CHECK_FALSE(static_cast<bool>(ned::editor::OrgMode().matchingDelimiters));
    CHECK_FALSE(static_cast<bool>(ned::editor::FundamentalMode().matchingDelimiters));
}

TEST_CASE("The capability shares the mode's parse rather than starting its own", "[ImprintBracket]") {
    // Not a timing assertion -- just that repeated calls on unchanged text are
    // served from the same incremental cache and stay consistent. The first
    // implementation parsed fresh per call, which was fine for a keystroke and
    // would have been a per-frame parse once a highlight consumed it.
    const ned::editor::Mode mode = ned::editor::CMode();
    REQUIRE(static_cast<bool>(mode.matchingDelimiters));

    const std::string text  = "int f(void) {\n    return 1;\n}\n";
    const std::size_t open  = text.find('{');
    const std::size_t close = text.rfind('}');

    for (int repeat = 0; repeat < 8; ++repeat) {
        const auto pair = mode.matchingDelimiters(text, open);
        REQUIRE(pair.has_value());
        CHECK(pair->openStart == open);
        CHECK(pair->closeStart == close);
    }
}

TEST_CASE("A keyword pair matches like a bracket pair", "[ImprintBracket]") {
    // Vim's matchit does `if`<->`fi`; ned gets it from the same table entry
    // that folds and indents the body, with nothing said per language.
    const std::string bash = "if true; then\n    echo hi\nfi\n";
    CHECK(Match("bash", bash, bash.find("if")) == bash.find("fi"));
    CHECK(Match("bash", bash, bash.find("fi")) == bash.find("if"));
    // The caret on the second letter of a multi-byte delimiter still counts.
    CHECK(Match("bash", bash, bash.find("fi") + 1) == bash.find("if"));

    const std::string fish = "function greet\n    echo hi\nend\n";
    CHECK(Match("fish", fish, fish.find("function")) == fish.find("end"));
    CHECK(Match("fish", fish, fish.find("end")) == 0);

    // `then` is not a closer, and `elif ... then` is a phrase the imprint
    // declines -- nothing to pair with.
    CHECK(Match("bash", bash, bash.find("then")) == std::nullopt);
}

TEST_CASE("A sigil-prefixed opener pairs with its bracket", "[ImprintBracket]") {
    // The tree's own token is `@[`, one anonymous child; the closer is `]`.
    const std::string janet = "(def xs @[\n  1\n  2])\n";
    CHECK(Match("janet", janet, janet.find("@[")) == janet.find(']'));
    CHECK(Match("janet", janet, janet.find(']')) == janet.find("@["));

    const std::string bash = "x=$(\n  ls\n)\n";
    CHECK(Match("bash", bash, bash.find("$(")) == bash.find(')'));

    const std::string js = "`a ${\n  b\n} c`\n";
    CHECK(Match("javascript", js, js.find("${")) == js.find('}'));
}
