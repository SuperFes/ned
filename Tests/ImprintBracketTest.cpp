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
