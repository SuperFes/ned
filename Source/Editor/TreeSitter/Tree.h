//
// RAII wrapper around a tree-sitter TSTree (tree-sitter foundation
// follow-up) -- see Node.h's header comment for the overall "why a
// hand-rolled wrapper" rationale.
//
// Move-only: a TSTree* is a unique, non-refcounted owning handle in the C
// API (ts_tree_copy exists for genuine deep copies, which this wrapper
// doesn't need yet and deliberately doesn't expose -- add it if/when a real
// caller needs to keep two independent trees alive from one parse).
//

#ifndef NED_EDITOR_TREESITTER_TREE_H
#define NED_EDITOR_TREESITTER_TREE_H

#include <tree_sitter/api.h>

#include "Node.h"

namespace ned::editor::treesitter {

class Tree {
  public:
    explicit Tree(TSTree* tree) noexcept; // takes ownership; tree may be nullptr (see IsNull())
    ~Tree();

    Tree(Tree&& other) noexcept;
    Tree& operator=(Tree&& other) noexcept;
    Tree(const Tree&)            = delete;
    Tree& operator=(const Tree&) = delete;

    // True if the wrapped TSTree* is null -- ts_parser_parse_string can
    // return null on a genuine parse failure (e.g. the parser was never
    // given a language). Every other member is only meaningful when this is
    // false.
    [[nodiscard]] bool IsNull() const noexcept;

    // Precondition: !IsNull(). The returned Node borrows from this Tree --
    // see Node.h's own lifetime warning.
    [[nodiscard]] Node RootNode() const;

    // Incremental-tree-sitter-reparse follow-up: records that the text this
    // tree was parsed from has been edited, per tree-sitter's own
    // ts_tree_edit contract -- mutates this Tree's internal byte/point
    // bookkeeping in place so a subsequent Parser::Parse(newText, this) can
    // reuse every subtree outside the edited range instead of reparsing from
    // scratch. Must be called (once per edit, in order) before that call; a
    // no-op if IsNull().
    void Edit(const TSInputEdit& edit) noexcept;

    // The raw TSTree*, for Parser::Parse's old-tree incremental-reparse
    // overload. Non-owning -- still owned by this Tree. Not for use outside
    // Source/Editor/TreeSitter/.
    [[nodiscard]] const TSTree* Raw() const noexcept;

  private:
    TSTree* tree_ = nullptr;
};

} // namespace ned::editor::treesitter

#endif // NED_EDITOR_TREESITTER_TREE_H
