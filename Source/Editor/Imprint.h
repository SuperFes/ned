//
// The imprint: what a language's own structure leaves behind, read rather
// than authored. Tier 0/1 vocabulary for the parsing-engine work
// (Docs/ParsingEngine.md).
//
// A tree-sitter grammar already states, for every rule, exactly how that
// construct is shaped. Nothing reads it back out: meaning is bolted on
// afterwards in per-language .scm queries, which is why 96% of every fold
// rule is restated verbatim as an indent rule and why 51% of the
// language x driver matrix is empty. This is the vocabulary those queries
// were approximating by hand.
//
// Nothing here knows tree-sitter exists. `TreeSitter/GrammarImprint.h` reads
// an imprint out of a grammar.json; these types are what it produces and what
// every consumer speaks. That split is the Phase 4 seam: if the engine is
// ever replaced, this file does not change.
//
// The vocabulary, used throughout the docs and worth knowing:
//
//  - **grain** -- structure readable from a construct's shape. The ordinary
//    case: 11 of 15 bundled locals queries dispatch purely on shape.
//  - **burl** -- a language or region whose grain is systematically tangled,
//    where structure must be read from *content* instead. Janet, Clojure and
//    Fish are the bundled ones (16, 15 and 6 text-predicates respectively,
//    against zero for C, Go, Java, Python and the rest): in a Lisp,
//    `(let [x 1] ...)`, `(defn f [a] ...)` and `(println x)` are the same
//    node type, and every binding form is a macro the grammar never heard
//    of. Not a special case to escape -- a different reading strategy for
//    the whole material, and the reason the Lisp cases are the right
//    acceptance test rather than an arbitrary one.
//  - **knot** -- a single point where the declarative rules do not reach and
//    a host callout is needed. Org's `*`-counted heading level, its
//    runtime-configured TODO keywords. Discrete, and rare by design.
//
// Worth stating plainly so it is not over-quoted: the 55/55 Tier 0 result is
// a *grain* result. It does not transfer to burls.
//

#ifndef NED_EDITOR_IMPRINT_H
#define NED_EDITOR_IMPRINT_H

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace ned::editor::imprint {

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

[[nodiscard]] std::string DelimiterKindName(DelimiterKind kind);

// The structural facts about one delimited body. Everything here is read off
// the grammar and nothing here is a decision -- a consumer that wants
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

// How a consumer turns the structural facts above into "should this node be
// offered as a fold". A policy object rather than a hardcoded rule because
// the answer is genuinely a matter of taste in at least one place, and the
// pathway to either answer should stay open.
//
// Measured across 11 grammars: 244 delimited bodies, 185 foldable with
// argument lists on, 144 with them off. So 59 are excluded by *structure* --
// a body holding exactly one subexpression has nothing to collapse, which is
// not a preference -- and only 41 are genuinely taste, all the
// argument/parameter-list family, behind this one flag rather than a
// per-language list.
//
// `foldArgumentLists` defaults ON: a long or overloaded signature is exactly
// where collapsing parameters helps, and the hand-written queries that omit
// them describe themselves as "deliberately minimal" rather than as having
// ruled them out. Cheap to reverse, and both directions are pinned by tests.
//
// Note what this does NOT decide: whether a body spans more than one line.
// That is a property of the text, not the grammar (`Editor/CodeFold.h`
// enforces it), and no amount of static policy can answer it.
struct FoldPolicy {
    bool foldArgumentLists = true;
};

// Whether `body` should be offered as a fold under `policy`. Pure; takes the
// inferred facts rather than a grammar, so a caller can reuse one imprint
// across several policies.
[[nodiscard]] bool ShouldFold(const DelimitedBody& body, const FoldPolicy& policy = {});


} // namespace ned::editor::imprint

#endif // NED_EDITOR_IMPRINT_H
