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

#include "../QueryData.h"
#include "Node.h"
#include "Parser.h"
#include "Query.h" // QueryCapture/QueryMatch -- the shared output vocabulary

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

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ned::editor::treesitter

#endif // NED_EDITOR_TREESITTER_QUERYMATCHER_H
