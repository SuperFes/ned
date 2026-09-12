//
// The imprint: what a language's own structure leaves behind, read rather
// than authored. Tier 0/1 vocabulary for the parsing-engine work
// (Docs/ParsingEngine.md).
//
// A tree-sitter grammar already states, for every rule, exactly how that
// construct is shaped. Nothing reads it back out: meaning is bolted on
// afterwards in per-language .scm queries, which is why 96% of every fold
// rule is restated verbatim as an indent rule and why 55% of the
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
#include <string_view>
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
    //
    // For an `Indent` body it answers the same question about a construct with
    // no bracket: does this node begin with its own introducer (Python's
    // `if_statement` opens with `if`, TOML's `table` with `[`, Python's
    // `string` with the scanner's own opening quote), or is it pure content
    // whose header line belongs to its parent (Python's `block`, YAML's
    // `block_mapping`)? The second kind is the only one whose first line is
    // not its own, which is what `FoldAnchorStart` below needs to know.
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
// The two delimiters of one delimited body, as byte ranges. `openStart` is
// always < `closeStart`. Lives here rather than beside the lookup that
// produces it because it is vocabulary, not machinery -- two byte ranges,
// nothing about a parser.
struct DelimiterPair {
    std::size_t openStart  = 0;
    std::size_t openEnd    = 0;
    std::size_t closeStart = 0;
    std::size_t closeEnd   = 0;
};

struct FoldPolicy {
    bool foldArgumentLists = true;
};

// Whether `body` should be offered as a fold under `policy`. Pure; takes the
// inferred facts rather than a grammar, so a caller can reuse one imprint
// across several policies.
[[nodiscard]] bool ShouldFold(const DelimitedBody& body, const FoldPolicy& policy = {});

// One direct child of a node, as the supersede rule below needs it: whether
// it is itself a foldable body, and the fold range it would contribute --
// `startByte` already anchored (see `FoldAnchorStart`).
struct ChildBody {
    bool        foldable  = false;
    std::size_t startByte = 0;
    std::size_t endByte   = 0;
};

// Whether a node's own fold should be skipped because one of its DIRECT
// children contributes the same fold.
//
// Fold the body, not the declaration. C#'s `namespace_declaration` is
// `namespace X { ... }` and its own direct child `declaration_list` is the
// `{ ... }`; both are genuinely delimited, and both fold from the row the
// `namespace X` is written on, so keeping both would put two affordances on
// one row for one construct.
//
// A node that introduces itself (`openerIsFirst`) hands its fold to any such
// child outright -- that is the C# case, where the child IS the node's body
// with the header stripped off, whether the brace sits on the header row or on
// the next one.
//
// A node with no introducer of its own cannot: its fold start was BORROWED
// from the row above (see `FoldAnchorStart`), and it must not hand that row to
// a child that begins further down. Python's `for` suite ends exactly where
// the `if` inside it does, and giving the row away left `for value in values:`
// with no affordance while the `if` had one. It still yields to a child whose
// fold starts at or above its own, which is YAML: a `block_node` wrapper
// begins on the row after the mapping it wraps has anchored to, and adds
// nothing.
//
// Compared by rows in neither direction -- the first version -- every level of
// nesting superseded the one above it, and a whole Python function reduced to
// its innermost statement.
//
// Takes the children's contributions and the text rather than a tree, so this
// stays free of any parser type. The caller walks; this decides. And an even
// earlier attempt is worth keeping recorded: it compared byte RANGES, dropping
// anything that shared an end byte with a later-starting range, which deleted
// Python class bodies -- a class body and its own last method's body
// legitimately share an end byte. Containment says nothing here; direct
// parentage does.
[[nodiscard]] bool SupersededByChildBody(const DelimitedBody& node, std::size_t nodeStartByte,
                                         std::size_t nodeEndByte, const std::vector<ChildBody>& children,
                                         std::string_view text);

// Where a fold over `[startByte, ...)` should really start, given the text it
// sits in.
//
// Every consumer of a fold block assumes one thing about its start byte: the
// row that byte lands on is the row left VISIBLE when the block collapses --
// a function's signature line, the line carrying the `{`. A bracket body
// satisfies that for free, because its opener is written on that row.
//
// An indentation body does not. Python's `block` starts at the first
// statement of the body, one row BELOW the `def f():` that names it, so
// folding it left the first statement on screen and hid the rest, and the
// gutter affordance sat on the wrong row. The fix is to say where the fold
// starts, not to teach every consumer a second rule.
//
// Two conditions, and each rules out a real case rather than a hypothetical:
//
//   - `openerIsFirst` must be false. Python's `if_statement` closes with a
//     dedent too, but `if x:` IS its header row; anchoring it upward would
//     move its fold onto the enclosing `def` line and lose it there.
//   - the body must start its own line, indented deeper than the nearest
//     non-blank line above. That line is then the header. TOML's `pair`
//     carries no introducer either, but `key = [` sits at its own
//     indentation, so nothing above it is a header to borrow.
//
// Pure: byte offsets and text, no parser, no grammar. Returns `startByte`
// unchanged whenever anchoring does not apply.
[[nodiscard]] std::size_t FoldAnchorStart(const DelimitedBody& body, std::size_t startByte, std::string_view text);

} // namespace ned::editor::imprint

#endif // NED_EDITOR_IMPRINT_H
