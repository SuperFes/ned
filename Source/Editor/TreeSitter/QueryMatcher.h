//
// Phase 4a: ned's own query matcher, consuming QueryData Forms directly
// against a tree-sitter parse tree -- the replacement for the
// ToQueryText -> ts_query_new -> TSQueryCursor round trip. Same output
// vocabulary as Query.h (QueryCapture/QueryMatch), same predicate semantics
// (QueryPredicates.h, shared code), same compile-time strictness (an
// unknown node type or field name throws -- Indent.h documentedly relies on
// a bad indents.janet failing loudly rather than matching nothing).
//
// Scope is the measured census (Tests/QueryMatcherTest.cpp M0), not the
// full query grammar: no '+' quantifier, quantifiers only on node patterns
// and bare wildcards, no top-level predicates, no anchors in alternations.
// A Form outside the supported set throws QueryMatcherError at compile, so
// scope growth is a loud decision, never silent misbehavior.
//
// Behavioral parity with the tree-sitter engine is pinned by the M3
// differential gate (both engines from the same Forms over the oracle
// corpus, ordered output compared byte-for-byte). Emission-order and
// enumeration policies that tree-sitter does not document are therefore
// centralized as named constants in QueryMatcher.cpp, each adjusted
// against the gate rather than guessed and scattered.
//

#ifndef NED_EDITOR_TREESITTER_QUERYMATCHER_H
#define NED_EDITOR_TREESITTER_QUERYMATCHER_H

#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <unordered_map>
#include "../QueryData.h"
#include "Node.h"
#include "Parser.h"

namespace ned::editor::treesitter {

// One capture from running a query against a tree -- name is the query
// pattern's capture name (e.g. "comment", "string", without the leading
// '@'), [startByte, endByte) is the captured node's byte range. nodeId
// (smart-indentation follow-up) is the captured node's own stable identity
// (Node::Id()) -- needed by any consumer that must tell apart two DIFFERENT
// nodes sharing the exact same byte range (real, not hypothetical: see
// Node::Id()'s own doc comment for tree-sitter-python's "block" node), since
// [startByte, endByte) alone can't disambiguate that case.
struct QueryCapture {
    std::string name;
    std::size_t startByte;
    std::size_t endByte;
    const void* nodeId;
};

// A capture within a QueryMatch -- same shape as QueryCapture, kept as a
// separate type since it lives inside QueryMatch::captures rather than a
// flat top-level vector (Captures() intentionally flattens match-grouping
// away; Matches() intentionally preserves it).
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
    // directive name (e.g. "injection.language" -> "javascript"). A
    // zero-operand #set! is stored with an empty value, so callers can
    // still test for the key's presence.
    std::unordered_map<std::string, std::string> setDirectives;
    // per-subtree-fact-memoization follow-up: true when this match's
    // pattern carries an actually-evaluated (not arity-inert)
    // (not-)has-ancestor?/(not-)has-parent? predicate -- see
    // QueryPredicates.h's PredicateReadsOutsideSubtree. A caller memoizing
    // facts per reused subtree must always fully re-derive a match with
    // this set, never reuse it across a reparse: the subtree it's attached
    // to can be byte-for-byte unchanged while its ancestry differs.
    bool ancestorCrossing = false;
};

} // namespace ned::editor::treesitter

namespace ned::editor::treesitter {

// Compile-time rejection: the 1-based source line of the offending Form
// (querydata keeps it through parsing), and a message naming what was
// wrong. Unlike QueryCompileError there is no byte offset to map back
// through generated text -- the line IS the real file's line, which is the
// point of consuming Forms directly.
class QueryMatcherError : public std::runtime_error {
  public:
    QueryMatcherError(int line, const std::string& message);
    [[nodiscard]] int Line() const {
        return line_;
    }

  private:
    int line_;
};

class QueryMatcher {
  public:
    // Compiles the patterns against the grammar. Throws QueryMatcherError
    // for an unknown node type, unknown field, a predicate referencing a
    // capture no pattern declares, or a Form construct outside the
    // supported (census-measured) set.
    QueryMatcher(const Language& language, std::span<const querydata::Form> forms);

    // Convenience for the current text-shaped seam (TreeSitterQuerySources
    // carries compiled query text): parses `source` as tree-sitter syntax
    // (querydata::ParseScm) and compiles the forms. QueryMatcherError lines
    // index into `source`.
    QueryMatcher(const Language& language, std::string_view source);
    ~QueryMatcher();

    QueryMatcher(QueryMatcher&& other) noexcept;
    QueryMatcher& operator=(QueryMatcher&& other) noexcept;
    QueryMatcher(const QueryMatcher&)            = delete;
    QueryMatcher& operator=(const QueryMatcher&) = delete;

    // Query.h's exact contracts, byte-for-byte -- see that header.
    [[nodiscard]] std::vector<QueryCapture> Captures(const Node& root, std::string_view sourceText) const;
    [[nodiscard]] std::vector<QueryCapture> CapturesInRange(const Node& root, std::string_view sourceText,
                                                            std::size_t startByte, std::size_t endByte) const;
    [[nodiscard]] std::vector<QueryMatch>   Matches(const Node& root, std::string_view sourceText) const;

    // Matches whose pattern ROOT node intersects [startByte, endByte) --
    // and, unlike CapturesInRange, each such match is emitted WHOLE: no
    // capture is filtered by the range, so an enclosing node's own captures
    // outside the window still arrive. The bound prunes tree traversal only.
    // What lets a viewport-sized symbol query still report the class that
    // opened hundreds of lines above the viewport (its node intersects the
    // window even though its @name does not).
    [[nodiscard]] std::vector<QueryMatch> MatchesInRange(const Node& root, std::string_view sourceText,
                                                         std::size_t startByte, std::size_t endByte) const;

    // per-subtree-fact-memoization follow-up: how many compiled patterns
    // carry an actually-evaluated (not arity-inert) (not-)has-ancestor?/
    // (not-)has-parent? predicate -- see QueryMatch::ancestorCrossing.
    // Diagnostic/test-only accessor, pinned by the query census the same
    // way the rest of the bundled corpus's measured construct surface is.
    [[nodiscard]] std::size_t AncestorCrossingPatternCount() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ned::editor::treesitter

#endif // NED_EDITOR_TREESITTER_QUERYMATCHER_H
