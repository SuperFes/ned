#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <vector>

#include "Editor/CodeFold.h"
#include "Editor/Imprint.h"
#include "Editor/Mode.h"
#include "Editor/TreeSitter/GrammarImprint.h"
#include "Editor/TreeSitter/Languages.h"
#include "Editor/TreeSitter/Node.h"
#include "Editor/TreeSitter/Parser.h"
#include "Editor/TreeSitter/Tree.h"

// Two halves, deliberately.
//
// The crafted-grammar cases pin each inference rule in isolation, so a
// failure names which shape broke. The corpus case is the Phase 1 gate from
// Docs/ParsingEngine.md: inference must still reproduce every one of the 55
// fold nodes hand-written across queries/*-folds.scm. That number was
// established by a Python spike that this superseded; enforcing it here is what
// makes a grammar bump that breaks inference fail the build instead of being
// discovered much later.

using ned::editor::imprint::DelimitedBody;
using ned::editor::imprint::DelimiterKind;
using ned::editor::imprint::FoldPolicy;
using ned::editor::imprint::ShouldFold;
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

std::set<std::string> HandWrittenNodes(const std::string& language, const std::string& kind,
                                       const std::string& capture) {
    const fs::path path =
        fs::path(NED_REPO_ROOT) / "Source" / "Editor" / "TreeSitter" / "queries" / (language + "-" + kind + ".scm");
    std::ifstream in(path);
    REQUIRE(in);
    std::ostringstream buffer;
    buffer << in.rdbuf();

    // Strip comments first -- several of these files are more prose than rule.
    const std::string    text = std::regex_replace(buffer.str(), std::regex(R"(;[^\n]*)"), "");
    std::set<std::string> nodes;
    const std::regex      capture1(R"(\(\s*([a-z_][a-z_0-9]*)\b[^()]*@)" + capture + R"()");
    for (auto it = std::sregex_iterator(text.begin(), text.end(), capture1); it != std::sregex_iterator(); ++it) {
        nodes.insert((*it)[1].str());
    }
    const std::regex bare(R"(\(\s*([a-z_][a-z_0-9]*)\s*\)\s*@)" + capture + R"()");
    for (auto it = std::sregex_iterator(text.begin(), text.end(), bare); it != std::sregex_iterator(); ++it) {
        nodes.insert((*it)[1].str());
    }
    return nodes;
}

} // namespace

TEST_CASE("A bracket-delimited production is inferred", "[Imprint]") {
    const json grammar = Grammar({{"body", Seq({Str("{"), Repeat(Sym("statement")), Str("}")})}});
    const auto found   = InferDelimitedBodies(grammar);
    REQUIRE(found.count("body") == 1);
    CHECK(found.at("body").kind == DelimiterKind::Bracket);
}

TEST_CASE("A closer may be followed by optional members", "[Imprint]") {
    // JavaScript's statement_block: SEQ['{', REPEAT(statement), '}', <optional>],
    // wrapped in a precedence. Requiring the closer to be literally last
    // misses it in JavaScript, TypeScript and TSX alike.
    const json grammar = Grammar(
        {{"statement_block", Prec(Seq({Str("{"), Repeat(Sym("statement")), Str("}"), Optional(Sym("semi"))}))}});
    CHECK(InferDelimitedBodies(grammar).count("statement_block") == 1);
}

TEST_CASE("Delimiters inside a hidden rule are inlined", "[Imprint]") {
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

TEST_CASE("An external closer with no opener is an indent-delimited body", "[Imprint]") {
    // Python's block: SEQ[REPEAT(_statement), _dedent]. The matching _indent
    // is consumed by the parent, so there is no opener to pair with.
    const json grammar = Grammar({{"block", Seq({Repeat(Sym("_statement")), Sym("_dedent")})}},
                                 json::array({Sym("_indent"), Sym("_dedent")}));
    const auto found = InferDelimitedBodies(grammar);
    REQUIRE(found.count("block") == 1);
    CHECK(found.at("block").kind == DelimiterKind::Indent);
}

TEST_CASE("Non-delimited and malformed productions are simply not reported", "[Imprint]") {
    CHECK(InferDelimitedBodies(Grammar({{"plain", Seq({Sym("a"), Sym("b")})}})).empty());
    CHECK(InferDelimitedBodies(Grammar({{"mismatched", Seq({Str("{"), Str(")")})}})).empty());
    CHECK(InferDelimitedBodies(Grammar({{"unopened", Seq({Repeat(Sym("x")), Str("}")})}})).empty());
    CHECK(InferDelimitedBodies(json::object()).empty());          // no "rules" at all
    CHECK(InferDelimitedBodies(json{{"rules", 42}}).empty());      // "rules" of the wrong type
}

TEST_CASE("A self-referential hidden rule terminates", "[Imprint]") {
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

TEST_CASE("Structural signals are reported, not pre-judged", "[Imprint]") {
    // A statement block opens its own production and holds a list.
    const json block = Grammar({{"block", Seq({Str("{"), Repeat(Sym("statement")), Str("}")})}});
    const auto b     = InferDelimitedBodies(block).at("block");
    CHECK(b.openerIsFirst);
    CHECK(b.listLikeInterior);

    // An argument list is list-like but does NOT open its own production --
    // the callee comes first. That is what makes it separable from a block.
    const json call = Grammar({{"call", Seq({Sym("callee"), Str("("), Optional(Sym("args")), Str(")")})}});
    const auto c    = InferDelimitedBodies(call).at("call");
    CHECK_FALSE(c.openerIsFirst);
    CHECK(c.listLikeInterior);

    // A parenthesized expression holds exactly one thing.
    const json paren = Grammar({{"paren", Seq({Str("("), Sym("expression"), Str(")")})}});
    const auto p     = InferDelimitedBodies(paren).at("paren");
    CHECK(p.openerIsFirst);
    CHECK_FALSE(p.listLikeInterior);
}

TEST_CASE("FoldPolicy keeps both answers reachable", "[Imprint]") {
    const DelimitedBody block{.kind = DelimiterKind::Bracket, .openerIsFirst = true, .listLikeInterior = true};
    const DelimitedBody argumentList{.kind = DelimiterKind::Bracket, .openerIsFirst = false, .listLikeInterior = true};
    const DelimitedBody paren{.kind = DelimiterKind::Bracket, .openerIsFirst = true, .listLikeInterior = false};

    // A single-element body is never a fold, under any policy -- that is
    // structure, not taste.
    CHECK_FALSE(ShouldFold(paren, FoldPolicy{.foldArgumentLists = true}));
    CHECK_FALSE(ShouldFold(paren, FoldPolicy{.foldArgumentLists = false}));

    // A statement block always is, likewise under any policy.
    CHECK(ShouldFold(block, FoldPolicy{.foldArgumentLists = true}));
    CHECK(ShouldFold(block, FoldPolicy{.foldArgumentLists = false}));

    // The argument list is the one the policy actually governs, and it
    // swings cleanly both ways. Default is on.
    CHECK(ShouldFold(argumentList, FoldPolicy{}));
    CHECK(ShouldFold(argumentList, FoldPolicy{.foldArgumentLists = true}));
    CHECK_FALSE(ShouldFold(argumentList, FoldPolicy{.foldArgumentLists = false}));
}

TEST_CASE("An opener may be a choice of literals", "[Imprint]") {
    // TypeScript's object_type opens with CHOICE["{", "{|"] -- one node, two
    // spellings. Treating an opener as a single string misses it.
    const json grammar = Grammar({{"object_type",
                                   Seq({json{{"type", "CHOICE"},
                                             {"members", json::array({Str("{"), Str("{|")})}},
                                        Repeat(Sym("member")), Str("}")})}});
    CHECK(InferDelimitedBodies(grammar).count("object_type") == 1);
}

TEST_CASE("Angle brackets delimit a body", "[Imprint]") {
    // A template/type parameter list is a real multi-element container that a
    // long declaration wraps across, and a JSX opening element is the same
    // shape.
    const json grammar = Grammar({{"type_parameters", Seq({Str("<"), Repeat(Sym("type_parameter")), Str(">")})}});
    CHECK(InferDelimitedBodies(grammar).count("type_parameters") == 1);
}

TEST_CASE("A token-wrapped rule is a leaf, not a delimited body", "[Imprint]") {
    // C's system_lib_string -- the <stdio.h> of an #include -- is a TOKEN
    // whose body happens to read as '<' repeat(...) '>'. Whatever structure is
    // written inside a TOKEN is not in the tree at all, so looking through one
    // finds delimiters in something with no interior to delimit.
    const json grammar = Grammar({{"system_lib_string",
                                   json{{"type", "TOKEN"},
                                        {"content", Seq({Str("<"), Repeat(Sym("chars")), Str(">")})}}}});
    CHECK(InferDelimitedBodies(grammar).empty());
}

TEST_CASE("Inference reproduces every hand-written fold node", "[Imprint][Corpus]") {
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
        const auto handWritten = HandWrittenNodes(language, "folds", "fold");
        expected += handWritten.size();

        for (const std::string& node : handWritten) {
            INFO("hand-written @fold node not inferred: " << language << " / " << node);
            CHECK(inferred.count(node) == 1);
            if (inferred.count(node) == 1) ++reproduced;
        }
    }

    // The same imprint also covers the hand-written @indent captures, which is
    // the N x M claim across two drivers rather than one: 96% of every fold
    // rule was already restated verbatim as an indent rule.
    std::size_t indentCovered = 0;
    std::size_t indentTotal   = 0;
    std::set<std::string> indentMissed;
    for (const auto& [language, relative] : kGrammars) {
        const fs::path path = DepsDir() / relative;
        const fs::path query =
            fs::path(NED_REPO_ROOT) / "Source" / "Editor" / "TreeSitter" / "queries" / (language + "-indents.scm");
        if (!fs::exists(path) || !fs::exists(query)) continue;
        std::ifstream in(path);
        json          grammar;
        in >> grammar;
        const auto inferred = InferDelimitedBodies(grammar);
        for (const std::string& node : HandWrittenNodes(language, "indents", "indent")) {
            ++indentTotal;
            if (inferred.count(node) == 1) ++indentCovered;
            else indentMissed.insert(language + "/" + node);
        }
    }
    // The single exception is real and is a defect in the query, not in
    // inference: tree-sitter-typescript has no `interface_body` rule at all
    // (its interface body is an `object_type`), so that capture can never
    // match anything. Recorded rather than worked around.
    INFO("indent nodes not covered: " << [&] {
        std::string joined;
        for (const std::string& node : indentMissed) joined += " " + node;
        return joined;
    }());
    CHECK(indentMissed == std::set<std::string>{"typescript/interface_body"});
    CHECK(indentCovered + 1 == indentTotal);

    INFO("reproduced " << reproduced << " of " << expected);
    // The corpus itself changed if this trips. It has once: cpp-folds.scm
    // gained (declaration_list) after inference reported a namespace body on
    // a real file that the hand-written list missed, taking 55 to 56.
    CHECK(expected == 56);
    CHECK(reproduced == expected);
}

// ---------------------------------------------------------------------------
// End-to-end: does the imprint actually DRIVE folding, not merely agree about
// node names?
//
// The corpus case above compares node type names, which is necessary and not
// sufficient -- names matching says nothing about the byte ranges a real parse
// produces. This walks a real tree, emits a fold range for every node whose
// type the imprint reports as foldable, and holds the result against what the
// hand-written .scm queries produce on the same file.
//
// Both sides get the multi-line rule applied, because that is the one part of
// "foldable" no static policy can answer (Editor/CodeFold.h enforces it on the
// real path) and comparing without it is not like-for-like.
//
// This is the Phase 2 claim in miniature: the hand-written fold queries are
// replaceable, not merely approximable.

namespace {

void CollectFoldable(const ned::editor::treesitter::Node&                                 node,
                     const std::map<std::string, ned::editor::imprint::DelimitedBody>&    bodies,
                     const ned::editor::imprint::FoldPolicy&                              policy,
                     std::vector<std::pair<std::size_t, std::size_t>>&                    out) {
    if (node.IsNull()) return;
    if (const auto it = bodies.find(std::string(node.Type())); it != bodies.end()) {
        if (ned::editor::imprint::ShouldFold(it->second, policy)) {
            out.emplace_back(node.StartByte(), node.EndByte());
        }
    }
    for (std::size_t i = 0; i < node.ChildCount(); ++i) CollectFoldable(node.Child(i), bodies, policy, out);
}

void KeepMultiLineOnly(std::vector<std::pair<std::size_t, std::size_t>>& blocks, const std::string& text) {
    std::erase_if(blocks, [&text](const std::pair<std::size_t, std::size_t>& block) {
        return text.find('\n', block.first) >= block.second;
    });
    std::sort(blocks.begin(), blocks.end());
}

} // namespace

TEST_CASE("An imprint reproduces the hand-written fold ranges on real files", "[Imprint][Corpus]") {
    struct Case {
        std::string             file;
        std::string             grammarDir;
        ned::editor::Mode       mode;
        std::string             language;
    };

    if (!fs::exists(DepsDir())) {
        SUCCEED("no build/_deps in this checkout -- grammars are FetchContent'd");
        return;
    }

    std::vector<Case> cases;
    cases.push_back({"sample.cpp", "tree-sitter-cpp-src", ned::editor::CppMode(), "cpp"});
    cases.push_back({"sample.py", "tree-sitter-python-src", ned::editor::PythonMode(), "python"});
    cases.push_back({"sample.json", "tree-sitter-json-src", ned::editor::JsonMode(), "json"});

    for (const Case& testCase : cases) {
        INFO("corpus file: " << testCase.file);

        const fs::path grammarPath = DepsDir() / testCase.grammarDir / "src" / "grammar.json";
        if (!fs::exists(grammarPath)) {
            WARN("missing grammar.json for " << testCase.file);
            continue;
        }
        std::ifstream grammarIn(grammarPath);
        REQUIRE(grammarIn);
        nlohmann::json grammar;
        grammarIn >> grammar;

        std::ifstream sourceIn(fs::path(NED_REPO_ROOT) / "Tests" / "Oracle" / "corpus" / testCase.file);
        REQUIRE(sourceIn);
        std::ostringstream sourceBuffer;
        sourceBuffer << sourceIn.rdbuf();
        const std::string text = sourceBuffer.str();

        const auto language = ned::editor::treesitter::LanguageByName(testCase.language);
        REQUIRE(language.has_value());
        const ned::editor::treesitter::Parser parser(*language);
        const ned::editor::treesitter::Tree   tree = parser.Parse(text);

        std::vector<std::pair<std::size_t, std::size_t>> inferred;
        CollectFoldable(tree.RootNode(), InferDelimitedBodies(grammar), ned::editor::imprint::FoldPolicy{}, inferred);
        KeepMultiLineOnly(inferred, text);

        auto handWritten = ned::editor::codefold::FoldableBlocks(testCase.mode, text);
        KeepMultiLineOnly(handWritten, text);

        INFO("inferred " << inferred.size() << " ranges, hand-written " << handWritten.size());
        CHECK(inferred == handWritten);
    }
}
