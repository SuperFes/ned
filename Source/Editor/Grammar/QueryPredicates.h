//
// The engine-neutral half of query predicate evaluation (Phase 4a matcher
// follow-up): the actual comparison semantics of #eq?/#match?/#lua-match?/
// #any-of?/#has-ancestor?-family predicates, over operands the caller has
// already resolved. Extracted from Query.cpp so the tree-sitter-backed
// Query and the Form-consuming QueryMatcher evaluate byte-for-byte the same
// predicates -- the two engines differ in how a predicate's operands are
// REPRESENTED (TSQueryPredicateStep vs. compiled Forms), never in what a
// predicate MEANS, and duplicating the quirk-laden evaluators (arity
// pass-throughs, unfired-capture no-ops, the Lua class translation) would
// let the two drift exactly where the differential gate needs them
// identical.
//
// Everything in here preserves Query.cpp's original behavior exactly,
// including the deliberate quirks: an unrecognized predicate name is inert
// (never suppresses a match); #eq?/#match?/#lua-match?/#any-of? are inert at
// any arity but their own fixed/variadic shape; and an operand whose capture
// never fired makes the predicate pass rather than false-compare against "".
// (not-)has-ancestor?/(not-)has-parent? are the one family whose arity is
// genuinely open-ended -- nvim's own convention accepts one or more type
// names after the capture, any-of semantics -- so a call with 2+ operands
// (first a fired capture, at least one trailing non-capture type operand)
// is evaluated for real; anything else (too few operands, no usable type
// operand) is the same inert pass-through as everywhere else in this file.
//

#ifndef NED_EDITOR_GRAMMAR_QUERYPREDICATES_H
#define NED_EDITOR_GRAMMAR_QUERYPREDICATES_H

#include <optional>
#include <regex>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>

#include "Editor/Parse/Node.h"

namespace ned::editor::grammar {

// One resolved predicate operand. `text` is the literal string for a
// string/token operand, or the captured node's own source text for a
// capture operand -- nullopt when the capture never fired in this match (a
// capture inside an optional/alternation branch), which every evaluator
// treats as "no-op, pass". `node` is set only for a fired capture operand;
// the has-ancestor family reads it, the text comparisons never do.
struct PredicateOperand {
    std::optional<std::string_view> text;
    parse::RedNode                  node      = parse::NodeNull();
    bool                            isCapture = false;

    // The has-ancestor family only: this operand's own ancestors, nearest
    // first, when the caller already knows them. NodeParent re-descends
    // from the tree root on every single step, so a caller that walks the
    // tree -- which therefore already holds the path it arrived by --
    // hands it over here instead. Empty means "caller has none", which is
    // also what a root node's chain looks like; both answer through the
    // NodeParent walk, which is one step for a root.
    std::span<const parse::RedNode> ancestors;

    // #match?/#lua-match?'s pattern operand only: the regex that operand's
    // text compiles to, precompiled once by the caller rather than
    // translated and hashed on every evaluation. Null asks
    // EvaluatePredicateCall to compile it itself (cached in regexCache);
    // `regexInvalid` says the caller tried and it does not compile, the
    // same inert pass-through a compile failure here produces.
    const std::regex* regex        = nullptr;
    bool              regexInvalid = false;
};

// Lua's %-prefixed character classes translated to the nearest ECMAScript
// bracket expression -- see Query.cpp's original comment for scope and the
// deliberate non-translation of everything else.
[[nodiscard]] std::string TranslateLuaPatternClasses(std::string pattern);

// One #match?/#lua-match? pattern compiled the way EvaluatePredicateCall
// compiles it -- Lua classes translated, inline "(?i)" lifted to the
// icase flag. nullopt for a pattern std::regex cannot parse, which the
// predicate treats as inert rather than as a failed match. Exposed so a
// caller holding the pattern ahead of time (a compiled query) can do this
// once per pattern instead of once per evaluation.
[[nodiscard]] std::optional<std::regex> CompilePredicateRegex(std::string_view pattern);

// Evaluates one "#name? operand..." call. True when the predicate passes
// AND when it isn't recognized (including #set! and nvim's capture-text
// directives) -- an unrecognized predicate must never suppress a match.
// `name` is accepted with or without its '#'/':' sigil.
[[nodiscard]] bool EvaluatePredicateCall(std::string_view name, std::span<const PredicateOperand> operands,
                                         std::unordered_map<std::string, std::regex>& regexCache);

// per-subtree-fact-memoization follow-up: whether a "#name? operand..."
// call, AS ACTUALLY EVALUATED (i.e. honoring the same arity-inert quirk
// EvaluatePredicateCall does -- a variadic has-parent?/has-ancestor? spelling
// with operandCount != 2 is inert, so it does NOT read outside the subtree,
// whatever it looks like it's asking), can make a pattern's result depend on
// structure outside the node it's attached to. Only the (not-)has-ancestor?/
// (not-)has-parent? family with exactly 2 operands qualifies -- every other
// recognized predicate (#eq?/#match?/#lua-match?/#any-of?) compares captured
// text, which is a property of the capture's own subtree, never its
// ancestry. A caller memoizing facts per reused subtree must treat any
// pattern this returns true for as always-recompute, never cached -- the
// same subtree can answer an ancestor query differently across reparses
// even when the subtree itself is byte-for-byte unchanged.
[[nodiscard]] bool PredicateReadsOutsideSubtree(std::string_view name, std::size_t operandCount);

// Whether a "#name? operand..." call, AS ACTUALLY EVALUATED, matches its
// first operand's text against the second one as a regex -- i.e. whether
// precompiling that second operand with CompilePredicateRegex is what this
// call will use. Lives here, beside the evaluator, so "which call is a
// regex call" is answered in one place rather than re-derived by whoever
// precompiles.
[[nodiscard]] bool PredicateMatchesRegex(std::string_view name, std::size_t operandCount);

} // namespace ned::editor::grammar

#endif // NED_EDITOR_GRAMMAR_QUERYPREDICATES_H
