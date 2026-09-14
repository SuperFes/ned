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
// (never suppresses a match); an unexpected arity is inert -- which today
// makes the variadic has-parent? spellings real files carry no-ops, see the
// ROADMAP watch list before "fixing" that; and an operand whose capture
// never fired makes the predicate pass rather than false-compare against "".
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
};

// Lua's %-prefixed character classes translated to the nearest ECMAScript
// bracket expression -- see Query.cpp's original comment for scope and the
// deliberate non-translation of everything else.
[[nodiscard]] std::string TranslateLuaPatternClasses(std::string pattern);

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

} // namespace ned::editor::grammar

#endif // NED_EDITOR_GRAMMAR_QUERYPREDICATES_H
