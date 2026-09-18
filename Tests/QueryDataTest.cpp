#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "Editor/LanguageFiles.h"
#include "Editor/QueryData.h"
#include "Editor/Grammar/Languages.h"
#include "Editor/Grammar/QueryMatcher.h"

using ned::editor::querydata::ConvertScmToJanet;
using ned::editor::querydata::Form;
using ned::editor::querydata::ParseJanet;
using ned::editor::querydata::ParseScm;
using ned::editor::querydata::QueryDataError;
using ned::editor::querydata::ToQueryText;

TEST_CASE("Janet-syntax query data reads to the same forms as tree-sitter's own", "[QueryData]") {
    const std::string       scm       = "; a comment\n(call_expression function: (identifier) @name (#eq? @name \"foo\")) @call\n"
                                        "[(a) (b)]* . (_)? !field \"lit\"+ _?\n";
    const std::string       janet     = "# a comment\n(call_expression function: (identifier) @name (:eq? @name \"foo\")) @call\n"
                                        "[(a) (b)]* . (_)? !field \"lit\"+ _?\n";
    const std::vector<Form> fromScm   = ParseScm(scm);
    const std::vector<Form> fromJanet = ParseJanet(janet);
    REQUIRE(fromScm == fromJanet);
    REQUIRE(fromScm.size() == 9); // comment, pattern, @call, alternation, anchor, (_)?, !field, "lit"+, _?
    REQUIRE(fromScm[0].kind == Form::Kind::Comment);
    REQUIRE(fromScm[1].kind == Form::Kind::List);
    REQUIRE(fromScm[1].items.back().kind == Form::Kind::Predicate);
    REQUIRE(fromScm[1].items.back().text == "eq?");
    REQUIRE(fromScm[2].text == "@call");
    REQUIRE(fromScm[3].kind == Form::Kind::Alternation);
    REQUIRE(fromScm[3].quantifier == '*');
    REQUIRE(fromScm[4].text == ".");
    REQUIRE(fromScm[5].kind == Form::Kind::List);
    REQUIRE(fromScm[5].quantifier == '?');
    REQUIRE(fromScm[6].text == "!field");
    REQUIRE(fromScm[7].kind == Form::Kind::String);
    REQUIRE(fromScm[7].quantifier == '+');
    REQUIRE(fromScm[8].text == "_");
    REQUIRE(fromScm[8].quantifier == '?');
}

TEST_CASE("Query text is emitted with each form on its source line", "[QueryData]") {
    const std::string janet = "# header\n\n(a) @x\n(b\n  (c) @y)\n";
    const std::string text  = ToQueryText(ParseJanet(janet));
    REQUIRE(text == "\n\n(a) @x\n(b\n(c) @y)\n");
    REQUIRE(ToQueryText(ParseJanet(janet), /*preserveLines=*/false) == "(a) @x\n(b (c) @y)\n");
}

TEST_CASE("The Janet reader rejects a file still in tree-sitter's spelling", "[QueryData]") {
    REQUIRE_THROWS_AS(ParseJanet("; a tree-sitter comment\n(a) @x"), QueryDataError);
    REQUIRE_THROWS_AS(ParseJanet("((a) @x (#eq? @x \"y\"))"), QueryDataError); // '#' comments out the rest of the line
    REQUIRE_THROWS_AS(ParseJanet("(a"), QueryDataError);
    REQUIRE_THROWS_AS(ParseJanet("* (a)"), QueryDataError); // a quantifier with nothing before it
    try {
        ParseJanet("(a)\n(b\n");
        FAIL("expected a parse error");
    }
    catch (const QueryDataError& error) {
        REQUIRE(error.Line() == 3);
    }
}

TEST_CASE("String escapes decode by tree-sitter's rules and re-encode by Janet's", "[QueryData]") {
    // tree-sitter: \d is a literal d; \n is a newline; \\ is a backslash.
    const std::string janet = ConvertScmToJanet("((x) (#match? @c \"^[A-Z\\d_]+$\")) \"a\\nb\" \"q\\\"\" \"\\\\d\"");
    REQUIRE(janet == "((x) (:match? @c \"^[A-Zd_]+$\")) \"a\\nb\" \"q\\\"\" \"\\\\d\"");
    const std::vector<Form> forms = ParseJanet(janet);
    REQUIRE(forms[0].items[1].items[1].text == "^[A-Zd_]+$");
    REQUIRE(forms[1].text == "a\nb");
    REQUIRE(forms[2].text == "q\"");
    REQUIRE(forms[3].text == "\\d");
    REQUIRE(ToQueryText(forms, false) == "((x) (#match? @c \"^[A-Zd_]+$\")) \"a\\nb\" \"q\\\"\" \"\\\\d\"\n");
}

TEST_CASE("Conversion preserves layout and comments byte for byte", "[QueryData]") {
    const std::string scm   = ";; Header ; with \"quotes\" and (#not a predicate)\n(a)   @x   ; trailing\n\n\n  [(b) (c)]\n";
    const std::string janet = ConvertScmToJanet(scm);
    REQUIRE(janet == "#; Header ; with \"quotes\" and (#not a predicate)\n(a)   @x   # trailing\n\n\n  [(b) (c)]\n");
    REQUIRE(ParseJanet(janet) == ParseScm(scm));
}

TEST_CASE("Every bundled query file loads, and compiles under its grammar", "[QueryData]") {
    std::size_t checked = 0;
    for (const ned::editor::BundledLanguageFile& file : ned::editor::BundledLanguageFiles()) {
        const std::string path(file.path);
        if (!path.ends_with(".janet") || path.ends_with("language.janet") || path.ends_with("grammar.janet")) {
            continue;
        }
        INFO(path);
        const std::vector<Form> forms = ParseJanet(file.content);
        REQUIRE_FALSE(forms.empty());
        // <name>/... -> the grammar it targets; jank has no files of its own.
        const std::string grammar  = path.substr(0, path.find('/'));
        const auto        language = ned::editor::grammar::LanguageByName(grammar);
        REQUIRE(language.has_value());
        const ned::editor::QueryText text = ned::editor::CompileQueryFiles({path});
        REQUIRE_NOTHROW(ned::editor::grammar::QueryMatcher(*language, text.text));
        ++checked;
    }
    REQUIRE(checked >= 90);
}
