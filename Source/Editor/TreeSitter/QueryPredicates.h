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

#ifndef NED_EDITOR_TREESITTER_QUERYPREDICATES_H
#define NED_EDITOR_TREESITTER_QUERYPREDICATES_H

#include <optional>
#include <regex>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>

#include <tree_sitter/api.h>

namespace ned::editor::treesitter {

// One resolved predicate operand. `text` is the literal string for a
// string/token operand, or the captured node's own source text for a
// capture operand -- nullopt when the capture never fired in this match (a
// capture inside an optional/alternation branch), which every evaluator
// treats as "no-op, pass". `node` is set only for a fired capture operand;
// the has-ancestor family reads it, the text comparisons never do.
struct PredicateOperand {
    std::optional<std::string_view> text;
    TSNode                          node      = TSNode{};
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

} // namespace ned::editor::treesitter

#endif // NED_EDITOR_TREESITTER_QUERYPREDICATES_H
