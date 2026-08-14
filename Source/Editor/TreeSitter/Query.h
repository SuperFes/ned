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

#ifndef NED_EDITOR_TREESITTER_QUERY_H
#define NED_EDITOR_TREESITTER_QUERY_H

#include <cstddef>
#include <string>
#include <string_view>
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
    // tree order. A fresh TSQueryCursor per call -- the simplest correct
    // thing; revisit only if a real [Performance] test says cursor reuse
    // matters, matching this project's own "prove it before optimizing"
    // discipline (see Parser.h's own note on full vs. incremental parsing).
    [[nodiscard]] std::vector<QueryCapture> Captures(const Node& root) const;

  private:
    TSQuery* query_ = nullptr;
};

} // namespace ned::editor::treesitter

#endif // NED_EDITOR_TREESITTER_QUERY_H
