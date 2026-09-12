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
#include <cctype>
#include <vector>

#include "Editor/CodeFold.h"
#include "Editor/Imprint.h"
#include "Editor/ImprintFold.h"
#include "Editor/ImprintTables.h"
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
// Docs/ParsingEngine.md: inference must still fold every one of the 59 nodes
// that queries/*-folds.scm named. Those files are deleted -- inference
// replaced them -- so the list they carried is pinned in this file instead,
// which is what still makes a grammar bump that breaks inference fail the
// build instead of being discovered much later.

using ned::editor::imprint::DelimitedBody;
using ned::editor::imprint::DelimiterKind;
using ned::editor::imprint::DelimiterKindName;
using ned::editor::imprint::FoldAnchorStart;
using ned::editor::imprint::FoldPolicy;
using ned::editor::imprint::ShouldFold;
using ned::editor::imprint::TableFor;
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
    const std::string text = std::regex_replace(buffer.str(), std::regex(R"(;[^\n]*)"), "");

    // Scanning back from each capture, balancing parens, rather than matching
    // a regex forward. A regex bounded by [^()]* cannot cross a nested group,
    // so it silently misses the conditional form -- `(function_body "{")
    // @fold`, which folds a Kotlin function only when it has a brace body
    // rather than `= expr`. Kotlin reported 7 hand-written fold nodes that way
    // when it has 10, which made this gate weaker than it claimed to be.
    //
    // Two shapes, and telling them apart is the whole job:
    //   `(declaration_list) @fold`      -- the capture follows a ')', so the
    //                                      captured node is the group that ')'
    //                                      closes.
    //   `(block "}" @dedent)`           -- the capture follows a token, so the
    //                                      node is the enclosing group's head.
    const std::string     needle = "@" + capture;
    std::set<std::string> nodes;

    const auto headOfGroupAt = [&text](std::size_t open) -> std::string {
        std::size_t head = open + 1;
        while (head < text.size() && std::isspace(static_cast<unsigned char>(text[head]))) ++head;
        std::size_t tail = head;
        while (tail < text.size() && (std::islower(static_cast<unsigned char>(text[tail])) ||
                                      std::isdigit(static_cast<unsigned char>(text[tail])) || text[tail] == '_')) {
            ++tail;
        }
        return tail > head ? text.substr(head, tail - head) : std::string{};
    };

    for (std::size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + 1)) {
        // A longer capture name that merely starts with this one is a
        // different capture: @indent.body is not @indent.
        const std::size_t after = at + needle.size();
        if (after < text.size() && (std::isalnum(static_cast<unsigned char>(text[after])) || text[after] == '.' ||
                                    text[after] == '_' || text[after] == '-')) {
            continue;
        }

        std::size_t cursor = at;
        while (cursor > 0 && std::isspace(static_cast<unsigned char>(text[cursor - 1]))) --cursor;
        if (cursor == 0) continue;

        std::string head;
        if (text[cursor - 1] == ')') {
            // Walk back to the '(' this ')' closes.
            int depth = 0;
            std::size_t scan = cursor - 1;
            while (true) {
                if (text[scan] == ')') ++depth;
                else if (text[scan] == '(' && --depth == 0) break;
                if (scan == 0) break;
                --scan;
            }
            if (text[scan] == '(') head = headOfGroupAt(scan);
        }
        else {
            // Walk back to the nearest unmatched '(' -- the enclosing group.
            int depth = 0;
            std::size_t scan = cursor;
            while (scan > 0) {
                --scan;
                if (text[scan] == ')') ++depth;
                else if (text[scan] == '(') {
                    if (depth == 0) break;
                    --depth;
                }
            }
            if (text[scan] == '(') head = headOfGroupAt(scan);
        }
        if (!head.empty()) nodes.insert(head);
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

TEST_CASE("An indentation body reports whether it has an introducer of its own", "[Imprint]") {
    // Two shapes close with a scanner token, and folding needs them apart.
    // A suite is pure content: the line naming it belongs to its parent.
    const json suite =
        Grammar({{"block", Seq({Repeat(Sym("statement")), Sym("_dedent")})}}, json::array({Sym("_dedent")}));
    const auto s = InferDelimitedBodies(suite).at("block");
    REQUIRE(s.kind == DelimiterKind::Indent);
    CHECK_FALSE(s.openerIsFirst);

    // A headed statement closes the same way but opens with its own keyword,
    // and `if x:` is its own first row.
    const json headed = Grammar({{"if_statement", Seq({Str("if"), Sym("condition"), Sym("_dedent")})}},
                                json::array({Sym("_dedent")}));
    const auto h      = InferDelimitedBodies(headed).at("if_statement");
    REQUIRE(h.kind == DelimiterKind::Indent);
    CHECK(h.openerIsFirst);

    // An external opener counts too -- Python's `string` is spelled
    // SEQ[string_start, ..., string_end], and string_start is the quote.
    const json quoted = Grammar({{"string", Seq({Sym("_start"), Repeat(Sym("content")), Sym("_end")})}},
                                json::array({Sym("_start"), Sym("_end")}));
    const auto q      = InferDelimitedBodies(quoted).at("string");
    REQUIRE(q.kind == DelimiterKind::Indent);
    CHECK(q.openerIsFirst);
}

TEST_CASE("FoldAnchorStart moves an indentation body onto its header row", "[Imprint]") {
    const DelimitedBody suite{.kind = DelimiterKind::Indent, .openerIsFirst = false, .listLikeInterior = true};
    const DelimitedBody headed{.kind = DelimiterKind::Indent, .openerIsFirst = true, .listLikeInterior = true};
    const DelimitedBody braced{.kind = DelimiterKind::Bracket, .openerIsFirst = true, .listLikeInterior = true};

    const std::string text = "def f():\n    x = 1\n    return x\n";
    const std::size_t body = text.find("x = 1");

    // The header is the line above, and the fold starts at its first byte.
    CHECK(FoldAnchorStart(suite, body, text) == 0);

    // A construct carrying its own header keeps its own start, or its fold
    // would jump onto the enclosing one and be lost there.
    CHECK(FoldAnchorStart(headed, body, text) == body);

    // A bracket body is already on the row its opener is written on.
    CHECK(FoldAnchorStart(braced, body, text) == body);

    // Equal indentation means the line above is a sibling, not a header --
    // TOML's `key = [` opens its own multi-line value.
    const std::string flat = "one = 1\ntwo = [\n  2,\n]\n";
    CHECK(FoldAnchorStart(suite, flat.find("two"), flat) == flat.find("two"));

    // A body beginning mid-line owns that line already.
    const std::string inline_ = "if x: foo(\n    bar)\n";
    CHECK(FoldAnchorStart(suite, inline_.find("foo"), inline_) == inline_.find("foo"));

    // Blank lines are stepped over rather than taken for the header.
    const std::string spaced = "def f():\n\n    x = 1\n";
    CHECK(FoldAnchorStart(suite, spaced.find("x = 1"), spaced) == 0);

    // Nothing above it: a top-level body owns its own first row.
    const std::string first = "    x = 1\n    y = 2\n";
    CHECK(FoldAnchorStart(suite, first.find("x = 1"), first) == first.find("x = 1"));
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

// The eleven `*-folds.scm` files this gate was written against are gone --
// deleted, not moved, because inference reproduces them: over 66 real files
// across those eleven languages the queries produced zero fold ranges the
// imprint did not, and every node type they named is a delimited body the
// shipping policy folds.
//
// Deleting them takes the ground truth with them, so it is pinned here
// instead. This list is what those queries said, verbatim, and it is
// deliberately frozen: it is not a thing to extend when a language gains a
// construct (the imprint already covers that), it is a tripwire. If a grammar
// bump renames `compound_statement` or makes `statements` non-list-like, this
// fails by name -- which is exactly what the corpus gate did when the files
// existed.
//
// The second half of each check is new and is the part the file-reading gate
// could never make: being INFERRED as delimited is not the same as being
// FOLDED. `ShouldFold` is the policy above `Delimited` (Editor/Imprint.h), and
// a node that fell out of it would still have passed the old gate while
// silently losing a fold.
const std::map<std::string, std::set<std::string>> kDeletedFoldQueries = {
    {"c", {"compound_statement", "field_declaration_list"}},
    {"clojure", {"anon_fn_lit", "list_lit", "map_lit", "read_cond_lit", "set_lit", "vec_lit"}},
    {"cpp", {"compound_statement", "declaration_list", "field_declaration_list"}},
    {"csharp",
     {"accessor_list", "block", "declaration_list", "enum_member_declaration_list", "initializer_expression",
      "switch_body", "switch_expression"}},
    {"go",
     {"block", "expression_switch_statement", "field_declaration_list", "interface_type", "literal_value",
      "select_statement", "type_switch_statement"}},
    {"java",
     {"annotation_type_body", "array_initializer", "block", "class_body", "constructor_body",
      "element_value_array_initializer", "enum_body", "interface_body", "module_body", "switch_block"}},
    {"javascript", {"class_body", "object", "statement_block"}},
    {"json", {"array", "object"}},
    {"kotlin",
     {"anonymous_initializer", "catch_block", "class_body", "control_structure_body", "enum_class_body",
      "finally_block", "function_body", "lambda_literal", "secondary_constructor", "when_expression"}},
    {"rust",
     {"block", "declaration_list", "enum_variant_list", "field_declaration_list", "field_initializer_list",
      "match_block"}},
    {"typescript", {"class_body", "object", "statement_block"}},
};

TEST_CASE("Every deleted fold query's nodes still fold from the imprint", "[Imprint][Corpus]") {
    // The Phase 1 gate, outliving the queries it was written against. See
    // Docs/ParsingEngine.md.
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
    for (const auto& [language, nodes] : kDeletedFoldQueries) {
        const fs::path path = DepsDir() / kGrammars.at(language);
        INFO("language: " << language << "  grammar: " << path.string());
        if (!fs::exists(path)) {
            WARN("missing grammar.json for " << language << " -- not counted");
            continue;
        }

        std::ifstream in(path);
        REQUIRE(in);
        json grammar;
        in >> grammar;

        const auto inferred = InferDelimitedBodies(grammar);
        expected += nodes.size();

        for (const std::string& node : nodes) {
            INFO("deleted @fold node no longer folds: " << language << " / " << node);
            const auto it = inferred.find(node);
            CHECK(it != inferred.end());
            if (it == inferred.end())
                continue;
            // Inferred AND folded. The old gate could only ask the first.
            CHECK(ShouldFold(it->second));
            if (ShouldFold(it->second))
                ++reproduced;
        }
    }

    INFO("reproduced " << reproduced << " of " << expected);
    // The pinned list itself changed if this trips, which it should not: it is
    // a record of eleven deleted files, not a live inventory. 59 was the count
    // the last of those files carried.
    CHECK(expected == 59);
    CHECK(reproduced == expected);
}

TEST_CASE("Inference reproduces every hand-written indent node", "[Imprint][Corpus]") {
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
    // Every hand-written @indent node is covered. No exceptions, and the last
    // one to fall is worth remembering: typescript/interface_body was recorded
    // here as a DEFECT IN THE QUERY -- "tree-sitter-typescript has no such
    // rule" -- and that was wrong. It is an alias of object_type, a perfectly
    // real node, invisible only because inference could not see alias() at the
    // time. The query was right all along.
    //
    // Worth the retelling because the failure mode is seductive: a measurement
    // that cannot see something reports its absence, and absence reads as the
    // other side's mistake.
    //
    // Two exceptions now, named rather than tolerated as a count, and both are
    // the SAME limit the fold work documented: a JSX element is
    // delimited by a matched `<li>`/`</li>` tag pair, which is not a bracket
    // pair, so the imprint has nothing to say about it and says nothing. Note
    // which JSX captures are NOT here -- `jsx_expression` (`{...}`) and
    // `jsx_opening_element` (`<...>`) are covered, because those genuinely are
    // brackets. The line falls exactly where the delimiter fact stops, which is
    // the useful thing to be able to see.
    const std::set<std::string> kTagDelimited = {"javascript/jsx_element", "javascript/jsx_self_closing_element"};
    CHECK(indentMissed == kTagDelimited);
    CHECK(indentCovered == indentTotal - kTagDelimited.size());
}

// ---------------------------------------------------------------------------
// End-to-end: does the CHECKED-IN table actually drive folding the way live
// inference does, not merely agree about map entries?
//
// The case above compares node types and their inferred signals, which is
// necessary and not sufficient -- entries matching says nothing about the byte
// ranges a real parse produces. This walks a real tree here in the test,
// emitting a fold range for every node live inference reports as foldable, and
// holds the result against what the shipping path produces from
// `Editor/ImprintTables.cpp`.
//
// It was written against the hand-written fold queries and outlived them: with
// those deleted, the two sides are `grammar.json` read at test time versus the
// artifact generated from it, and the walk below is a second implementation of
// `ImprintFold.cpp`'s rather than a reuse of it. Both halves are the point --
// a stale checked-in table shows up as a range diff, and the walk itself is
// cross-checked by an independent one.
//
// Both sides get the multi-line rule applied, because that is the one part of
// "foldable" no static policy can answer (Editor/CodeFold.h enforces it on the
// real path) and comparing without it is not like-for-like.
//

namespace {

bool IsFoldable(const ned::editor::treesitter::Node&                              node,
                const std::map<std::string, ned::editor::imprint::DelimitedBody>& bodies,
                const ned::editor::imprint::FoldPolicy&                           policy) {
    if (node.IsNull()) return false;
    const auto it = bodies.find(std::string(node.Type()));
    return it != bodies.end() && ned::editor::imprint::ShouldFold(it->second, policy);
}

// Fold the body, not the declaration.
//
// C#'s `namespace_declaration` is `namespace X { ... }` and its own direct
// child `declaration_list` is the `{ ... }`. Both are genuinely delimited and
// inference is right to report both, but folding the declaration hides the
// `namespace X` line itself -- the one line you still want while the body is
// away, exactly as a function's signature is.
//
// The test is "has a direct child that is also foldable and ends where I do",
// and it has to be asked of the TREE. A first attempt compared byte ranges
// instead -- drop anything sharing an end byte with a later-starting range --
// and it was wrong in a way worth recording: a Python class body and its own
// last method's body legitimately share an end byte, so the class body
// vanished. Containment says nothing; direct parentage does.
bool HasFoldableBodyChild(const ned::editor::treesitter::Node&                              node,
                          const std::map<std::string, ned::editor::imprint::DelimitedBody>& bodies,
                          const ned::editor::imprint::FoldPolicy&                           policy,
                          std::string_view                                                  text) {
    const auto anchored = [&](const ned::editor::treesitter::Node& n) {
        const auto it = bodies.find(std::string(n.Type()));
        return it == bodies.end() ? n.StartByte()
                                  : ned::editor::imprint::FoldAnchorStart(it->second, n.StartByte(), text);
    };
    std::vector<ned::editor::imprint::ChildBody> children;
    for (std::size_t i = 0; i < node.ChildCount(); ++i) {
        const ned::editor::treesitter::Node child = node.Child(i);
        const bool                          folds = IsFoldable(child, bodies, policy);
        children.push_back(ned::editor::imprint::ChildBody{folds, folds ? anchored(child) : child.StartByte(),
                                                           child.EndByte()});
    }
    const auto self = bodies.find(std::string(node.Type()));
    return ned::editor::imprint::SupersededByChildBody(self->second, anchored(node), node.EndByte(),
                                                       children, text);
}

void CollectFoldable(const ned::editor::treesitter::Node&                              node,
                     const std::map<std::string, ned::editor::imprint::DelimitedBody>& bodies,
                     const ned::editor::imprint::FoldPolicy&                           policy,
                     std::string_view                                                  text,
                     std::vector<std::pair<std::size_t, std::size_t>>&                 out) {
    if (node.IsNull()) return;
    if (IsFoldable(node, bodies, policy) && !HasFoldableBodyChild(node, bodies, policy, text)) {
        const auto it = bodies.find(std::string(node.Type()));
        out.emplace_back(ned::editor::imprint::FoldAnchorStart(it->second, node.StartByte(), text), node.EndByte());
    }
    for (std::size_t i = 0; i < node.ChildCount(); ++i)
        CollectFoldable(node.Child(i), bodies, policy, text, out);
}

// Both sides of every comparison below go through the same text-level rules a
// real consumer does -- see CodeFold.h's NormalizeFoldBlocks for what they are
// and why they are not the fold source's business.
void Normalize(std::vector<std::pair<std::size_t, std::size_t>>& blocks, const std::string& text) {
    blocks = ned::editor::codefold::NormalizeFoldBlocks(std::move(blocks), text);
}

} // namespace

// The compiled-in table (Editor/ImprintTables.cpp) is generated from the same
// grammars this test reads. Holding the two against each other on every run is
// what keeps a checked-in artifact from serving yesterday's answer.
//
// Regenerate with NED_BLESS_IMPRINT=1 and read the diff.
TEST_CASE("The compiled-in imprint table matches live inference", "[Imprint][Corpus]") {
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
        // Languages with NO hand-written folds.scm at all. They get folding
        // from the imprint alone -- which is the whole N x M argument arriving:
        // nine languages gaining a feature because the grammar already said
        // enough, with nothing authored per language.
        {"bash", "tree-sitter-bash-src/src/grammar.json"},
        {"css", "tree-sitter-css-src/src/grammar.json"},
        {"fish", "tree-sitter-fish-src/src/grammar.json"},
        {"html", "tree-sitter-html-src/src/grammar.json"},
        {"janet", "tree-sitter-janet-simple-src/src/grammar.json"},
        {"php", "tree-sitter-php-src/php/src/grammar.json"},
        {"toml", "tree-sitter-toml-src/src/grammar.json"},
        {"xml", "tree-sitter-xml-src/xml/src/grammar.json"},
        {"yaml", "tree-sitter-yaml-src/src/grammar.json"},
        {"tsx", "tree-sitter-typescript-src-src/tsx/src/grammar.json"},
        // jank shares Clojure's grammar outright, but a table is keyed by the
        // MODE's language key rather than by the grammar, so it needs its own
        // entry -- it had none, and folded only because it also shared
        // clojure-folds.scm. Deleting that query is what surfaced it, and
        // bracket matching (gated on the same table) had been silently missing
        // for jank all along.
        {"jank", "tree-sitter-clojure-src/src/grammar.json"},
    };

    if (!fs::exists(DepsDir())) {
        SUCCEED("no build/_deps in this checkout -- grammars are FetchContent'd");
        return;
    }

    std::map<std::string, std::map<std::string, DelimitedBody>> live;
    for (const auto& [language, relative] : kGrammars) {
        const fs::path path = DepsDir() / relative;
        if (!fs::exists(path)) continue;
        std::ifstream in(path);
        REQUIRE(in);
        json grammar;
        in >> grammar;
        live[language] = InferDelimitedBodies(grammar);
    }

    if (std::getenv("NED_BLESS_IMPRINT") != nullptr) {
        std::ostringstream out;
        out << "// GENERATED by Tests/ImprintTest.cpp -- do not edit by hand.\n"
            << "//\n"
            << "// Regenerate with:  NED_BLESS_IMPRINT=1 ./build/ned_tests \"[Imprint]\"\n"
            << "// See Editor/ImprintTables.h for why this is checked in rather than\n"
            << "// derived at build time, and what guards it against going stale.\n\n"
            << "#include \"Editor/ImprintTables.h\"\n\n"
            << "namespace ned::editor::imprint {\n\nnamespace {\n\n"
            << "struct Entry {\n"
            << "    std::string_view node;\n"
            << "    DelimiterKind    kind;\n"
            << "    bool             openerIsFirst;\n"
            << "    bool             listLikeInterior;\n"
            << "};\n\n";

        for (const auto& [language, bodies] : live) {
            out << "constexpr Entry k" << static_cast<char>(std::toupper(language[0])) << language.substr(1)
                << "[] = {\n";
            for (const auto& [node, body] : bodies) {
                out << "    {\"" << node << "\", DelimiterKind::" << DelimiterKindName(body.kind) << ", "
                    << (body.openerIsFirst ? "true" : "false") << ", "
                    << (body.listLikeInterior ? "true" : "false") << "},\n";
            }
            out << "};\n\n";
        }

        out << "const std::map<std::string, std::map<std::string, DelimitedBody>>& Tables() {\n"
            << "    static const std::map<std::string, std::map<std::string, DelimitedBody>> kTables = [] {\n"
            << "        std::map<std::string, std::map<std::string, DelimitedBody>> built;\n"
            << "        const auto load = [&built](std::string_view language, const Entry* entries,\n"
            << "                                   std::size_t count) {\n"
            << "            auto& table = built[std::string(language)];\n"
            << "            for (std::size_t i = 0; i < count; ++i) {\n"
            << "                table.emplace(std::string(entries[i].node),\n"
            << "                              DelimitedBody{entries[i].kind, entries[i].openerIsFirst,\n"
            << "                                            entries[i].listLikeInterior});\n"
            << "            }\n"
            << "        };\n";
        for (const auto& [language, bodies] : live) {
            const std::string symbol =
                "k" + std::string(1, static_cast<char>(std::toupper(language[0]))) + language.substr(1);
            out << "        load(\"" << language << "\", " << symbol << ", std::size(" << symbol << "));\n";
        }
        out << "        return built;\n    }();\n    return kTables;\n}\n\n"
            << "} // namespace\n\n"
            << "const std::map<std::string, DelimitedBody>& TableFor(std::string_view language) {\n"
            << "    static const std::map<std::string, DelimitedBody> kEmpty;\n"
            << "    const auto it = Tables().find(std::string(language));\n"
            << "    return it == Tables().end() ? kEmpty : it->second;\n}\n\n"
            << "std::vector<std::string> TabledLanguages() {\n"
            << "    std::vector<std::string> names;\n"
            << "    for (const auto& [language, table] : Tables()) names.push_back(language);\n"
            << "    return names;\n}\n\n"
            << "} // namespace ned::editor::imprint\n";

        std::ofstream file(fs::path(NED_REPO_ROOT) / "Source" / "Editor" / "ImprintTables.cpp",
                           std::ios::binary | std::ios::trunc);
        REQUIRE(file);
        file << out.str();
        SUCCEED("regenerated ImprintTables.cpp -- read the diff");
        return;
    }

    for (const auto& [language, bodies] : live) {
        INFO("language: " << language);
        const auto& compiled = TableFor(language);
        INFO("compiled-in " << compiled.size() << " entries, live " << bodies.size()
                            << " (regenerate: NED_BLESS_IMPRINT=1 ./build/ned_tests \"[Imprint]\")");
        REQUIRE(compiled.size() == bodies.size());
        for (const auto& [node, body] : bodies) {
            INFO("node: " << node);
            const auto it = compiled.find(node);
            REQUIRE(it != compiled.end());
            CHECK(it->second.kind == body.kind);
            CHECK(it->second.openerIsFirst == body.openerIsFirst);
            CHECK(it->second.listLikeInterior == body.listLikeInterior);
        }
    }
}

TEST_CASE("The compiled table folds real files exactly as live inference does", "[Imprint][Corpus]") {
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
    cases.push_back({"sample.c", "tree-sitter-c-src", ned::editor::CMode(), "c"});
    cases.push_back({"sample.cpp", "tree-sitter-cpp-src", ned::editor::CppMode(), "cpp"});
    cases.push_back({"sample.py", "tree-sitter-python-src", ned::editor::PythonMode(), "python"});
    cases.push_back({"sample.json", "tree-sitter-json-src", ned::editor::JsonMode(), "json"});
    cases.push_back({"sample.clj", "tree-sitter-clojure-src", ned::editor::ClojureMode(), "clojure"});
    cases.push_back({"sample.go", "tree-sitter-go-src", ned::editor::GoMode(), "go"});
    cases.push_back({"sample.rs", "tree-sitter-rust-src", ned::editor::RustMode(), "rust"});
    cases.push_back({"sample.java", "tree-sitter-java-src", ned::editor::JavaMode(), "java"});
    cases.push_back({"sample.cs", "tree-sitter-c-sharp-src", ned::editor::CSharpMode(), "csharp"});
    cases.push_back({"sample.js", "tree-sitter-javascript-src", ned::editor::JavaScriptMode(), "javascript"});
    cases.push_back(
        {"sample.ts", "tree-sitter-typescript-src-src/typescript", ned::editor::TypeScriptMode(), "typescript"});
    cases.push_back({"sample.kt", "tree-sitter-kotlin-src", ned::editor::KotlinMode(), "kotlin"});

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
        CollectFoldable(tree.RootNode(), InferDelimitedBodies(grammar), ned::editor::imprint::FoldPolicy{}, text,
                        inferred);
        Normalize(inferred, text);

        // The shipping path: a Mode whose fold source is the compiled-in
        // table, run through the same FoldableBlocks every consumer uses.
        ned::editor::Mode viaImprint = testCase.mode;
        viaImprint.fold = ned::editor::imprint::BuildFoldFunction(testCase.language);
        REQUIRE(static_cast<bool>(viaImprint.fold));
        auto compiled = ned::editor::codefold::FoldableBlocks(viaImprint, text);
        Normalize(compiled, text);

        INFO("live inference " << inferred.size() << " ranges, compiled table " << compiled.size());
        if (inferred != compiled) {
            for (const auto& range : inferred) {
                if (std::find(compiled.begin(), compiled.end(), range) == compiled.end()) {
                    std::string snippet = text.substr(range.first, std::min<std::size_t>(44, range.second - range.first));
                    for (char& ch : snippet) if (ch == '\n') ch = ' ';
                    WARN("  ONLY-INFERRED " << range.first << ".." << range.second << "  \"" << snippet << "\"");
                }
            }
            for (const auto& range : compiled) {
                if (std::find(inferred.begin(), inferred.end(), range) == inferred.end()) {
                    std::string snippet = text.substr(range.first, std::min<std::size_t>(44, range.second - range.first));
                    for (char& ch : snippet) if (ch == '\n') ch = ' ';
                    WARN("  ONLY-TABLE    " << range.first << ".." << range.second << "  \"" << snippet << "\"");
                }
            }
        }
        CHECK(inferred == compiled);
    }
}

TEST_CASE("Languages that never had a fold query fold from the imprint alone", "[Imprint]") {
    // The N x M argument arriving as a feature rather than a number: none of
    // these ever had a hand-written fold query, and every one of them folds
    // because the grammar already said enough. Nothing was authored per
    // language.
    //
    // Every language is in that position now -- the eleven that did have a
    // query no longer do -- but these four are the ones that never needed one
    // written in the first place, which is the claim worth keeping separate.
    struct Case {
        ned::editor::Mode mode;
        std::string       name;
        std::string       text;
        std::size_t       expected;
    };

    std::vector<Case> cases;
    cases.push_back({ned::editor::PhpMode(), "php",
                     "<?php\nclass Widget {\n    public function size() {\n        return 1;\n    }\n}\n", 2});
    cases.push_back({ned::editor::CssMode(), "css", "body {\n    color: red;\n    margin: 0;\n}\n", 1});
    cases.push_back({ned::editor::BashMode(), "bash", "run() {\n    echo hi\n    echo bye\n}\n", 1});
    cases.push_back({ned::editor::TomlMode(), "toml", "[table]\nkey = [\n  1,\n  2,\n]\n", 2});

    for (const Case& testCase : cases) {
        INFO("language: " << testCase.name);
        const auto blocks = ned::editor::codefold::FoldableBlocks(testCase.mode, testCase.text);
        CHECK(blocks.size() == testCase.expected);
    }
}

TEST_CASE("A language whose delimiters are not brackets contributes nothing, and that is correct",
          "[Imprint]") {
    // HTML's element is `start_tag ... end_tag` -- matched tag pairs, not
    // bracket literals -- so the imprint has nothing to say about it and says
    // nothing. No special case anywhere expresses that; it falls out of the
    // table being empty for constructs it cannot read.
    //
    // This is the same shape as Org folding by headline depth and Markdown by
    // section structure. The compose mechanism (MergeFoldSources) exists so a
    // language like this can supply its own fold source later and have it
    // simply work alongside whatever else is present.
    const auto html   = ned::editor::HtmlMode();
    const auto blocks = ned::editor::codefold::FoldableBlocks(html, "<div>\n  <p>\n    hello\n  </p>\n</div>\n");
    CHECK(blocks.empty());

    // YAML used to be the second entry here, on the reasoning that its block
    // structure "is not expressed as rules at all". That was also wrong: its
    // rules are simply all hidden and its node names come from alias(), so
    // block_mapping and block_sequence were invisible rather than absent. It
    // folds now -- nested, depth-first, exactly like Python -- and is asserted
    // as such below.
    const std::string source  = "root:\n  child:\n    - one\n    - two\n  other: 3\n";
    const auto        yaml    = ned::editor::YamlMode();
    const auto        blocks2 = ned::editor::codefold::FoldableBlocks(yaml, source);

    // Asserted as rows rather than as a count, which is what a reader sees and
    // what the count quietly got wrong: `stream`, `document` and the top-level
    // `block_mapping` are three nodes but one fold, and the mapping's own node
    // begins on `  child:` -- a row it does not own. Each block folds from the
    // line that NAMES it: `root:` hides its children, `  child:` hides the
    // sequence under it.
    std::vector<std::size_t> rows;
    for (const auto& block : blocks2) {
        rows.push_back(static_cast<std::size_t>(std::count(source.begin(), source.begin() + block.first, '\n')));
    }
    CHECK(rows == std::vector<std::size_t>{0, 1});
}
