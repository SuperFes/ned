//
// RAII wrapper around a tree-sitter TSQuery (tree-sitter foundation
// follow-up) -- see Node.h's header comment for the overall "why a
// hand-rolled wrapper" rationale.
//
// This is the piece that turns a parse tree into highlighting: a query is
// the grammar-specific "@comment"/"@string"/... pattern source (each
// bundled grammar ships its own under queries/highlights.scm -- see the
// bundle-remaining-grammars follow-up), and Captures() is what a Mode's
// highlighter runs per repaint to get a flat list of (name, byte range)
// pairs to map onto SyntaxClass.
//
// Predicate evaluation (generic-tree-sitter-highlighting follow-up):
// Captures() evaluates the common tree-sitter query predicates
// (#eq?/#not-eq?, #match?/#not-match?/#lua-match?/#not-lua-match?,
// #any-of?/#not-any-of?, #has-ancestor?/#has-parent?/#not-has-ancestor?/
// #not-has-parent?) against each match before including its captures -- a
// real capability this project didn't have before (Captures() used to
// evaluate none of them at all, unconditionally including every match).
// Any OTHER predicate name, including the non-filtering #set! directive
// real query files use for match priority, is inert: a predicate this
// doesn't recognize never suppresses a match, matching the pre-existing
// "include everything" default exactly for whatever it doesn't understand
// yet -- see Query.cpp's own comment on why that's the only safe default.
//

#ifndef NED_EDITOR_TREESITTER_QUERY_H
#define NED_EDITOR_TREESITTER_QUERY_H

#include <cstddef>
#include <regex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <tree_sitter/api.h>

#include "Node.h"
#include "Parser.h"

namespace ned::editor::treesitter {

// One capture from running a Query against a tree -- name is the query
// pattern's capture name (e.g. "comment", "string", without the leading
// '@'), [startByte, endByte) is the captured node's byte range.
struct QueryCapture {
    std::string name;
    std::size_t startByte;
    std::size_t endByte;
};

// A capture within a QueryMatch -- same shape as QueryCapture, kept as a
// separate type since it lives inside QueryMatch::captures rather than a
// flat top-level vector (Captures() intentionally flattens match-grouping
// away; Matches() below intentionally preserves it).
struct QueryMatchCapture {
    std::string name;
    std::size_t startByte;
    std::size_t endByte;
};

// One matched pattern instance, with its captures kept together (unlike
// Captures()'s flat output) and its #set! directives resolved -- for
// consumers (language-injection resolution) that need several captures from
// one pattern instance correlated, e.g. pairing a dynamic
// "@injection.language" capture with its sibling "@injection.content"
// capture in the SAME fenced code block, not some other one in the document.
struct QueryMatch {
    std::vector<QueryMatchCapture> captures;
    // Resolved #set! operands for this match's pattern, keyed by the
    // directive name (e.g. "injection.language" -> "javascript" for
    // `(#set! injection.language "javascript")`). A zero-operand #set! (e.g.
    // `(#set! injection.combined)`) is stored with an empty value, so
    // callers can still test for the key's presence.
    std::unordered_map<std::string, std::string> setDirectives;
};

class Query {
  public:
    // Throws std::runtime_error, with the byte offset into source and a
    // description of the error kind, if source is malformed (a query source
    // referencing a node/field name the grammar doesn't have is a query
    // error here, same as a syntax error in the query language itself --
    // ts_query_new reports both through the same error_offset/error_type
    // pair).
    Query(const Language& language, std::string_view source);
    ~Query();

    Query(Query&& other) noexcept;
    Query& operator=(Query&& other) noexcept;
    Query(const Query&)            = delete;
    Query& operator=(const Query&) = delete;

    // Runs this query against root's subtree, returning every capture in
    // tree order, from matches whose own predicates (see this class's own
    // header comment) all evaluated true. A fresh TSQueryCursor per call --
    // the simplest correct thing; revisit only if a real [Performance] test
    // says cursor reuse matters, matching this project's own "prove it
    // before optimizing" discipline (see Parser.h's own note on full vs.
    // incremental parsing). sourceText is the exact same buffer text root
    // was parsed from -- needed so text-comparison predicates
    // (#eq?/#match?/#any-of?) have something to read a captured node's
    // actual text out of; every real caller already has this in scope
    // (a Mode's HighlightFunction is itself handed the buffer's full text).
    [[nodiscard]] std::vector<QueryCapture> Captures(const Node& root, std::string_view sourceText) const;

    // Match-grouped view of the same run Captures() does (same predicate
    // filtering, same "unrecognized predicate never suppresses a match"
    // default) -- for a consumer that needs several captures from one
    // pattern instance correlated together, which #eq?/#match?/etc.-only
    // consumers never needed. See QueryMatch's own doc comment.
    [[nodiscard]] std::vector<QueryMatch> Matches(const Node& root, std::string_view sourceText) const;

  private:
    TSQuery* query_ = nullptr;

    // #match?/#lua-match? predicate cache, keyed by the (translated)
    // ECMAScript pattern text: std::regex construction is genuinely slow --
    // real, measured, not assumed, see the cmake-highlighting-perf
    // follow-up -- and a query file can carry dozens of #match? predicates,
    // each evaluated once per matching node in the buffer. Without this, a
    // real-world file with a few hundred matches against a pattern-heavy
    // query (tree-sitter-cmake's own highlights.scm, ~15 distinct #match?
    // patterns) recompiled the same handful of regexes hundreds of times
    // over, the actual cause of a multi-second first-paint stall. Mutable
    // since Captures() is logically const (querying doesn't change what the
    // query IS) but still wants to memoize across calls; safe without a
    // mutex because a Mode's HighlightFunction (and the Query it closes
    // over) is only ever invoked from BufferView::Paint on the main/UI
    // thread, never concurrently.
    mutable std::unordered_map<std::string, std::regex> regexCache_;
};

} // namespace ned::editor::treesitter

#endif // NED_EDITOR_TREESITTER_QUERY_H
