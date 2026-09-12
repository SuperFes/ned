#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

#include "Editor/TreeSitter/TraitInference.h"

// Two halves, deliberately.
//
// The crafted-grammar cases pin each inference rule in isolation, so a
// failure names which shape broke. The corpus case is the Phase 1 gate from
// Docs/ParsingEngine.md: inference must still reproduce every one of the 55
// fold nodes hand-written across queries/*-folds.scm. That number was
// established by Tools/TraitInferenceProbe.py; enforcing it here is what
// makes a grammar bump that breaks inference fail the build instead of being
// discovered much later.

using ned::editor::treesitter::DelimiterKind;
using ned::editor::treesitter::InferDelimitedBodies;
using nlohmann::json;

namespace {

namespace fs = std::filesystem;

json Seq(std::initializer_list<json> members) {
    return json{{"type", "SEQ"}, {"members", json(members)}};
}
json Str(const std::string& value) { return json{{"type", "STRING"}, {"value", value}}; }
json Sym(const std::string& name) { return json{{"type", "SYMBOL"}, {"name", name}}; }
json Repeat(const json& content) { return json{{"type", "REPEAT"}, {"content", content}}; }
json Optional(const json& content) {
    return json{{"type", "CHOICE"}, {"members", json::array({content, json{{"type", "BLANK"}}})}};
}
json Prec(const json& content) { return json{{"type", "PREC_RIGHT"}, {"value", 0}, {"content", content}}; }

json Grammar(const json& rules, const json& externals = json::array()) {
    return json{{"rules", rules}, {"externals", externals}};
}

// The FetchContent tree the real grammars live in. Absent in a source-only
// checkout, so the corpus case skips rather than fails there.
fs::path DepsDir() { return fs::path(NED_REPO_ROOT) / "build" / "_deps"; }

std::set<std::string> HandWrittenFoldNodes(const std::string& language) {
    const fs::path path =
        fs::path(NED_REPO_ROOT) / "Source" / "Editor" / "TreeSitter" / "queries" / (language + "-folds.scm");
    std::ifstream in(path);
    REQUIRE(in);
    std::ostringstream buffer;
    buffer << in.rdbuf();

    // Strip comments first -- several of these files are more prose than rule.
    const std::string    text = std::regex_replace(buffer.str(), std::regex(R"(;[^\n]*)"), "");
    std::set<std::string> nodes;
    const std::regex      capture(R"(\(\s*([a-z_][a-z_0-9]*)\b[^()]*@fold)");
    for (auto it = std::sregex_iterator(text.begin(), text.end(), capture); it != std::sregex_iterator(); ++it) {
        nodes.insert((*it)[1].str());
    }
    const std::regex bare(R"(\(\s*([a-z_][a-z_0-9]*)\s*\)\s*@fold)");
    for (auto it = std::sregex_iterator(text.begin(), text.end(), bare); it != std::sregex_iterator(); ++it) {
        nodes.insert((*it)[1].str());
    }
    return nodes;
}

} // namespace

TEST_CASE("A bracket-delimited production is inferred", "[TraitInference]") {
    const json grammar = Grammar({{"body", Seq({Str("{"), Repeat(Sym("statement")), Str("}")})}});
    const auto found   = InferDelimitedBodies(grammar);
    REQUIRE(found.count("body") == 1);
    CHECK(found.at("body") == DelimiterKind::Bracket);
}

TEST_CASE("A closer may be followed by optional members", "[TraitInference]") {
    // JavaScript's statement_block: SEQ['{', REPEAT(statement), '}', <optional>],
    // wrapped in a precedence. Requiring the closer to be literally last
    // misses it in JavaScript, TypeScript and TSX alike.
    const json grammar = Grammar(
        {{"statement_block", Prec(Seq({Str("{"), Repeat(Sym("statement")), Str("}"), Optional(Sym("semi"))}))}});
    CHECK(InferDelimitedBodies(grammar).count("statement_block") == 1);
}

TEST_CASE("Delimiters inside a hidden rule are inlined", "[TraitInference]") {
    // Clojure's list_lit is SEQ[REPEAT(_metadata_lit), _bare_list_lit], with
    // the parens one level down. tree-sitter inlines hidden rules rather than
    // making them nodes, so inference must too -- without this all six
    // Clojure fold nodes are invisible.
    const json grammar = Grammar({
        {"list_lit", Seq({Repeat(Sym("_metadata_lit")), Sym("_bare_list_lit")})},
        {"_bare_list_lit", Seq({Str("("), Repeat(Sym("form")), Str(")")})},
        {"_metadata_lit", Str("^")},
    });
    const auto found = InferDelimitedBodies(grammar);
    CHECK(found.count("list_lit") == 1);
    CHECK(found.count("_bare_list_lit") == 0); // hidden rules are never nodes
}

TEST_CASE("An external closer with no opener is an indent-delimited body", "[TraitInference]") {
    // Python's block: SEQ[REPEAT(_statement), _dedent]. The matching _indent
    // is consumed by the parent, so there is no opener to pair with.
    const json grammar = Grammar({{"block", Seq({Repeat(Sym("_statement")), Sym("_dedent")})}},
                                 json::array({Sym("_indent"), Sym("_dedent")}));
    const auto found = InferDelimitedBodies(grammar);
    REQUIRE(found.count("block") == 1);
    CHECK(found.at("block") == DelimiterKind::Indent);
}

TEST_CASE("Non-delimited and malformed productions are simply not reported", "[TraitInference]") {
    CHECK(InferDelimitedBodies(Grammar({{"plain", Seq({Sym("a"), Sym("b")})}})).empty());
    CHECK(InferDelimitedBodies(Grammar({{"mismatched", Seq({Str("{"), Str(")")})}})).empty());
    CHECK(InferDelimitedBodies(Grammar({{"unopened", Seq({Repeat(Sym("x")), Str("}")})}})).empty());
    CHECK(InferDelimitedBodies(json::object()).empty());          // no "rules" at all
    CHECK(InferDelimitedBodies(json{{"rules", 42}}).empty());      // "rules" of the wrong type
}

TEST_CASE("A self-referential hidden rule terminates", "[TraitInference]") {
    // Inlining recurses, so a grammar whose hidden rule references itself
    // must not spin or blow the stack. Only termination is asserted: this
    // production genuinely does contain "{" before "}" once inlining stops,
    // so reporting it as delimited is defensible and pinning either answer
    // would be testing an accident of the depth limit rather than a rule.
    const json cyclic = Grammar({{"r", Seq({Sym("_x"), Str("}")})}, {"_x", Seq({Sym("_x"), Str("{")})}});
    CHECK_NOTHROW(InferDelimitedBodies(cyclic));

    // Mutual recursion between two hidden rules, same requirement.
    const json mutual = Grammar({{"r", Seq({Sym("_a"), Str("}")})},
                                {"_a", Seq({Sym("_b"), Str("{")})},
                                {"_b", Seq({Sym("_a"), Str("[")})}});
    CHECK_NOTHROW(InferDelimitedBodies(mutual));
}

TEST_CASE("Inference reproduces every hand-written fold node", "[TraitInference][Corpus]") {
    // The Phase 1 gate. See Docs/ParsingEngine.md.
    const std::map<std::string, std::string> kGrammars = {
        {"c", "tree-sitter-c-src/src/grammar.json"},
        {"cpp", "tree-sitter-cpp-src/src/grammar.json"},
        {"csharp", "tree-sitter-c-sharp-src/src/grammar.json"},
        {"go", "tree-sitter-go-src/src/grammar.json"},
        {"java", "tree-sitter-java-src/src/grammar.json"},
        {"javascript", "tree-sitter-javascript-src/src/grammar.json"},
        {"json", "tree-sitter-json-src/src/grammar.json"},
        {"kotlin", "tree-sitter-kotlin-src/src/grammar.json"},
        {"python", "tree-sitter-python-src/src/grammar.json"},
        {"rust", "tree-sitter-rust-src/src/grammar.json"},
        {"typescript", "tree-sitter-typescript-src-src/typescript/src/grammar.json"},
        {"clojure", "tree-sitter-clojure-src/src/grammar.json"},
    };

    if (!fs::exists(DepsDir())) {
        SUCCEED("no build/_deps in this checkout -- grammars are FetchContent'd");
        return;
    }

    std::size_t reproduced = 0;
    std::size_t expected   = 0;
    for (const auto& [language, relative] : kGrammars) {
        const fs::path path = DepsDir() / relative;
        INFO("language: " << language << "  grammar: " << path.string());
        if (!fs::exists(path)) {
            WARN("missing grammar.json for " << language << " -- not counted");
            continue;
        }

        std::ifstream in(path);
        REQUIRE(in);
        json grammar;
        in >> grammar;

        const auto inferred  = InferDelimitedBodies(grammar);
        const auto handWritten = HandWrittenFoldNodes(language);
        expected += handWritten.size();

        for (const std::string& node : handWritten) {
            INFO("hand-written @fold node not inferred: " << language << " / " << node);
            CHECK(inferred.count(node) == 1);
            if (inferred.count(node) == 1) ++reproduced;
        }
    }

    INFO("reproduced " << reproduced << " of " << expected);
    CHECK(expected == 55);     // the corpus itself changed if this trips
    CHECK(reproduced == expected);
}
