#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

#include "Editor/Grammar/Compile/GrammarFile.h"
#include "Editor/LanguageFiles.h"

using ned::editor::grammar::compile::GrammarFile;
using ned::editor::grammar::compile::GrammarFileError;
using ned::editor::grammar::compile::ParseGrammarJanet;
using ned::editor::grammar::compile::ParseGrammarJson;
using Rule = ned::editor::grammar::compile::GrammarFile::Rule;
using ned::editor::grammar::compile::ToGrammarJanet;

namespace {

namespace fs = std::filesystem;

std::string ReadFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

Rule Sym(std::string name) {
    Rule rule;
    rule.kind = Rule::Kind::Symbol;
    rule.text = std::move(name);
    return rule;
}

Rule Str(std::string text) {
    Rule rule;
    rule.kind = Rule::Kind::String;
    rule.text = std::move(text);
    return rule;
}

Rule Wrap(Rule::Kind kind, Rule child) {
    Rule rule;
    rule.kind = kind;
    rule.children.push_back(std::move(child));
    return rule;
}

Rule Members(Rule::Kind kind, std::vector<Rule> children) {
    Rule rule;
    rule.kind     = kind;
    rule.children = std::move(children);
    return rule;
}

// Every form the vocabulary has, once.
GrammarFile Everything() {
    GrammarFile grammar;
    grammar.name = "probe";
    grammar.word = "identifier";

    Rule pattern;
    pattern.kind  = Rule::Kind::Pattern;
    pattern.text  = "[a-z]+\\s\"";
    pattern.flags = "i";

    Rule prec                = Wrap(Rule::Kind::PrecLeft, Members(Rule::Kind::Seq, {Sym("expression"), Str("+"), Sym("expression")}));
    prec.precedence          = 3;
    Rule namedPrec           = Wrap(Rule::Kind::Prec, Sym("nil"));
    namedPrec.precedenceName = "unary";
    Rule dynamicPrec         = Wrap(Rule::Kind::PrecDynamic, Sym("true"));
    dynamicPrec.precedence   = -1;
    Rule alias               = Wrap(Rule::Kind::Alias, Sym("_hidden"));
    alias.text               = "visible";
    alias.named              = true;
    Rule anonAlias           = Wrap(Rule::Kind::Alias, Str("=>"));
    anonAlias.text           = "arrow";
    anonAlias.named          = false;
    Rule field               = Wrap(Rule::Kind::Field, Sym("identifier"));
    field.text               = "name";
    Rule reserved            = Wrap(Rule::Kind::Reserved, Sym("identifier"));
    reserved.text            = "properties";

    Rule blank;
    grammar.rules = {
        {"program", Wrap(Rule::Kind::Repeat, Sym("expression"))},
        {"expression", Members(Rule::Kind::Choice, {prec, namedPrec, dynamicPrec, alias, anonAlias, field, reserved, Wrap(Rule::Kind::Repeat1, Sym("nil")), Wrap(Rule::Kind::Token, pattern), Wrap(Rule::Kind::TokenImmediate, Str("x")), blank})},
        // Memberless forms are forms, not atoms: (:choice) must still emit.
        {"empty", Members(Rule::Kind::Seq, {Members(Rule::Kind::Choice, {}), Members(Rule::Kind::Seq, {})})},
        {"identifier", pattern},
        {"nil", Str("nil")},
        {"true", Str("true")},
        {"_hidden", Sym("identifier")},
    };
    grammar.extras      = {pattern, Sym("comment")};
    grammar.externals   = {Sym("comment"), Str("raw"), pattern};
    grammar.conflicts   = {{"expression", "_hidden"}, {"nil"}};
    grammar.precedences = {{Str("unary"), Sym("expression")}, {Str("a"), Str("b")}};
    grammar.inlineRules = {"_hidden"};
    grammar.supertypes  = {"expression"};
    grammar.reserved    = {{"global", {Str("break"), pattern}}, {"properties", {}}};
    return grammar;
}

} // namespace

TEST_CASE("A precedence is an integer symbol or a named precedence", "[GrammarFile]") {
    const GrammarFile grammar = ParseGrammarJanet("{:name \"x\" :rules {a (:prec -2 b) c (:prec-left 10 d) e (:prec \"tight\" f)}}");
    REQUIRE(grammar.rules.size() == 3);
    CHECK(grammar.rules[0].second.precedence == -2);
    CHECK(grammar.rules[1].second.precedence == 10);
    CHECK(grammar.rules[2].second.precedenceName == "tight");
    CHECK_THROWS_WITH(ParseGrammarJanet("{:name \"x\" :rules {a (:prec 1.5 b)}}"), Catch::Matchers::ContainsSubstring("precedence is an integer"));
}

TEST_CASE("Every grammar form round-trips through grammar.janet text", "[GrammarFile]") {
    const GrammarFile grammar = Everything();
    const std::string text    = ToGrammarJanet(grammar);
    INFO(text);
    const GrammarFile back = ParseGrammarJanet(text);
    CHECK(back == grammar);
    // Names Janet would read as data go through (:ref ...), everything else
    // stays a bare symbol.
    CHECK_THAT(text, Catch::Matchers::ContainsSubstring("(:ref \"nil\")"));
    CHECK_THAT(text, Catch::Matchers::ContainsSubstring("(:ref \"true\")"));
    CHECK_THAT(text, Catch::Matchers::ContainsSubstring(" identifier "));
    // A second emission of the re-read model is byte-identical: the file
    // format is a fixed point.
    CHECK(ToGrammarJanet(back) == text);
}

TEST_CASE("grammar.json and grammar.janet read to the same model", "[GrammarFile]") {
    const nlohmann::ordered_json json     = nlohmann::ordered_json::parse(R"({
        "name": "mini",
        "word": "identifier",
        "rules": {
            "program": {"type": "REPEAT", "content": {"type": "SYMBOL", "name": "statement"}},
            "statement": {"type": "PREC_RIGHT", "value": "stmt", "content": {"type": "SEQ", "members": [
                {"type": "FIELD", "name": "name", "content": {"type": "SYMBOL", "name": "identifier"}},
                {"type": "ALIAS", "content": {"type": "STRING", "value": ";"}, "named": false, "value": "end"},
                {"type": "CHOICE", "members": [{"type": "IMMEDIATE_TOKEN", "content": {"type": "STRING", "value": "!"}}, {"type": "BLANK"}]},
                {"type": "RESERVED", "context_name": "global", "content": {"type": "SYMBOL", "name": "identifier"}}
            ]}},
            "identifier": {"type": "PATTERN", "value": "[a-z]+", "flags": "i"}
        },
        "extras": [{"type": "PATTERN", "value": "\\s"}],
        "conflicts": [["program", "statement"]],
        "precedences": [[{"type": "STRING", "value": "stmt"}, {"type": "SYMBOL", "name": "program"}]],
        "externals": [{"type": "SYMBOL", "name": "raw"}],
        "inline": ["statement"],
        "supertypes": [],
        "reserved": {"global": [{"type": "STRING", "value": "if"}]}
    })");
    const GrammarFile            fromJson = ParseGrammarJson(json);
    const std::string            janet    = R"(
        {:name "mini"
         :word identifier
         :extras [(:pattern "\\s")]
         :conflicts [[program statement]]
         :precedences [["stmt" program]]
         :externals [raw]
         :inline [statement]
         :supertypes []
         :reserved {:global ["if"]}
         :rules
         {program (:repeat statement)
          statement (:prec-right "stmt"
                      (:seq (:field :name identifier)
                            (:alias ";" "end")
                            (:choice (:token-immediate "!") :blank)
                            (:reserved :global identifier)))
          identifier (:pattern "[a-z]+" "i")}}
    )";
    CHECK(ParseGrammarJanet(janet) == fromJson);
    CHECK(ParseGrammarJanet(ToGrammarJanet(fromJson)) == fromJson);
}

TEST_CASE("Malformed grammar.janet fails with the line and the mistake", "[GrammarFile]") {
    CHECK_THROWS_WITH(ParseGrammarJanet("{:name \"x\" :rules {a nil}}"), Catch::Matchers::ContainsSubstring("(:ref \"nil\")"));
    CHECK_THROWS_WITH(ParseGrammarJanet("{:name \"x\" :rules {a (:prec x b)}}"), Catch::Matchers::ContainsSubstring("precedence is an integer"));
    CHECK_THROWS_WITH(ParseGrammarJanet("{:name \"x\" :rules {a (:frob b)}}"), Catch::Matchers::ContainsSubstring("unknown rule form (:frob"));
    CHECK_THROWS_WITH(ParseGrammarJanet("{:name \"x\" :rules {a (:repeat b c)}}"), Catch::Matchers::ContainsSubstring("takes 1 argument"));
    CHECK_THROWS_WITH(ParseGrammarJanet("{:name \"x\"}"), Catch::Matchers::ContainsSubstring(":rules is required"));
    CHECK_THROWS_WITH(ParseGrammarJanet("{:name \"x\" :rules {a (:field name b)}}"), Catch::Matchers::ContainsSubstring("field name is a :keyword"));
    try {
        [[maybe_unused]] const GrammarFile parsed = ParseGrammarJanet("{:name \"x\"\n :rules\n {a (:seq b\n        (:choice))\n  c (:token)}}");
        FAIL("expected an error");
    }
    catch (const GrammarFileError& error) {
        CHECK(error.Line() == 5);
    }
}

TEST_CASE("Every bundled language with a grammar has a readable grammar.janet", "[GrammarFile]") {
    std::size_t found = 0;
    for (const auto& entry : fs::directory_iterator(ned::editor::BundledLanguagesRoot())) {
        const fs::path file = entry.path() / "grammar.janet";
        if (!fs::exists(file)) {
            continue;
        }
        INFO(file.string());
        const GrammarFile grammar = ParseGrammarJanet(ReadFile(file));
        CHECK_FALSE(grammar.name.empty());
        CHECK_FALSE(grammar.rules.empty());
        ++found;
    }
    CHECK(found == 122); // 37 at the divorce, +74 across the 2026-09-18 pump batches, +11 across the 2026-09-21 config/language batches
}
