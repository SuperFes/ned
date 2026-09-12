//
// Tier 0 of the parsing-engine work (Docs/ParsingEngine.md): structural
// traits derived from a grammar itself, with no per-language rules written.
//
// A tree-sitter grammar.json describes every rule's production. That is
// enough to recognize a *delimited body* -- a node that opens with one
// token and closes with its partner -- which is the single fact behind
// folding, indent/dedent, structural selection, brace matching and
// sticky-scroll containers. Today each of those is a hand-written .scm
// listing node names per language, and 96% of every fold rule is restated
// verbatim as an indent rule.
//
// Measured against the 55 fold nodes currently hand-written across
// queries/*-folds.scm: this reproduces 55/55 with nothing authored per
// language. Tools/TraitInferenceProbe.py is the original spike and scores
// the same corpus; TraitInferenceTest.cpp enforces the number so a grammar
// bump that breaks inference fails the build rather than being noticed
// later.
//
// This infers Delimited, NOT Foldable. Over the same corpus it also reports
// 211 nodes beyond the hand-written 55 -- parenthesized_expression in nine
// languages, argument_list in six, string_literal. Every one is genuinely
// delimited and none is something to fold. Deciding *which* delimited nodes
// a given feature wants is Tier 1 policy sitting on top of this, and it is
// a handful of mostly cross-language exclusions rather than a list of node
// names per language. Keeping that line sharp is the point: this layer
// reports structure with high recall and no taste.
//
// Pure: a function over parsed JSON, no Parser, Buffer or Screen, so it is
// unit-testable against crafted grammars as well as real ones.
//

#ifndef NED_EDITOR_TREESITTER_TRAITINFERENCE_H
#define NED_EDITOR_TREESITTER_TRAITINFERENCE_H

#include <map>
#include <string>

#include <nlohmann/json.hpp>

namespace ned::editor::treesitter {

// How a node's body is closed. Two shapes, because indentation languages
// genuinely differ rather than being a special case of brackets.
enum class DelimiterKind {
    // Opens with a bracket token and closes with its partner: `{ ... }`,
    // `( ... )`, `[ ... ]`.
    Bracket,
    // Closes with an external (scanner-produced) token and has no opener of
    // its own -- Python's `block` is SEQ[REPEAT(_statement), _dedent], with
    // the matching _indent consumed by the parent rule.
    Indent,
};

// Every visible rule in grammar that is a delimited body, mapped to how it
// closes. Hidden (_-prefixed) rules are never reported: tree-sitter inlines
// them rather than making them nodes, so they can never be a node a
// consumer folds or selects.
//
// Throws nothing -- a grammar missing "rules", or carrying a shape this
// doesn't understand, yields fewer entries rather than an error. An
// unparseable grammar is the caller's problem at json::parse time.
// The structural facts about one delimited body. Everything here is read
// off the grammar and nothing here is a decision -- a consumer that wants
// "foldable" or "selectable" or "indentable" combines these itself.
//
// Carrying the signals rather than pre-judging them is deliberate. Measured
// over the hand-written fold corpus, `openerIsFirst` alone would drop 16 of
// the 55 nodes those queries name, so it is emphatically not a filter to
// apply blindly -- but it is exactly what separates `block` ('{' first) from
// `index_expression` (an expression, then '['), and a later policy will want
// it. Deciding here would throw that away.
struct DelimitedBody {
    DelimiterKind kind = DelimiterKind::Bracket;

    // The opener is the production's first member. False for a node that
    // carries content before its own bracket -- `index_expression`,
    // `array_declarator`, Kotlin's `catch_block`.
    bool openerIsFirst = false;

    // Between the delimiters sits a REPEAT or a CHOICE rather than a single
    // element: the difference between `'{' repeat(statement) '}'` and
    // `'(' expression ')'`. A body that can hold a *list* of things is the
    // one worth collapsing; one holding exactly one subexpression is not.
    bool listLikeInterior = false;
};

[[nodiscard]] std::map<std::string, DelimitedBody> InferDelimitedBodies(const nlohmann::json& grammar);

[[nodiscard]] std::string DelimiterKindName(DelimiterKind kind);

// How a consumer turns the structural facts above into "should this node be
// offered as a fold". A policy object rather than a hardcoded rule because
// the answer is genuinely a matter of taste in at least one place, and the
// pathway to either answer should stay open.
//
// `foldArgumentLists` is that place. An argument or parameter list is a
// list-like bracketed body that does not open its own production (the callee
// or declarator comes first), so it is separable from a statement block, and
// reasonable editors disagree about it. Defaulted ON: a long or overloaded
// signature is exactly where collapsing the parameters helps, and the
// hand-written queries that omit it describe themselves as "deliberately
// minimal" rather than as having ruled it out.
//
// Note what this does NOT decide: whether a body spans more than one line.
// That is a property of the text, not the grammar (`Editor/CodeFold.h`
// enforces it), and no amount of static policy can answer it.
struct FoldPolicy {
    bool foldArgumentLists = true;
};

// Whether `body` should be offered as a fold under `policy`. Pure; takes the
// inferred facts rather than a grammar, so a caller can reuse one inference
// result across several policies.
[[nodiscard]] bool ShouldFold(const DelimitedBody& body, const FoldPolicy& policy = {});

} // namespace ned::editor::treesitter

#endif // NED_EDITOR_TREESITTER_TRAITINFERENCE_H
