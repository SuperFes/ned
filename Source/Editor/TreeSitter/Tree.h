//
// The parse-tree handle the editor holds -- since the Phase 4b engine swap
// a thin wrapper over ned's own parse::GreenTree rather than a TSTree*.
// See Node.h's header comment for the overall wrapper rationale.
//
// Kept move-only even though GreenTree itself is cheaply copyable: every
// existing consumer was written against the original unique-handle
// semantics, and keeping them is what makes the swap invisible.
//

#ifndef NED_EDITOR_TREESITTER_TREE_H
#define NED_EDITOR_TREESITTER_TREE_H

#include "Editor/Parse/Tree.h"

#include "Node.h"

namespace ned::editor::treesitter {

class Tree {
  public:
    explicit Tree(parse::GreenTree tree) noexcept; // tree may be null (see IsNull())
    ~Tree();

    Tree(Tree&& other) noexcept;
    Tree& operator=(Tree&& other) noexcept;
    Tree(const Tree&)            = delete;
    Tree& operator=(const Tree&) = delete;

    // True if this holds no tree -- the engine returns one for every parse,
    // so ordinarily only reachable through a moved-from Tree. Every other
    // member is only meaningful when this is false.
    [[nodiscard]] bool IsNull() const noexcept;

    // Precondition: !IsNull(). The returned Node borrows from this Tree --
    // see Node.h's own lifetime warning.
    [[nodiscard]] Node RootNode() const;

    // Incremental-tree-sitter-reparse follow-up: records that the text this
    // tree was parsed from has been edited -- adjusts this Tree's byte/point
    // bookkeeping in place so a subsequent Parser::Parse(newText, this) can
    // reuse every subtree outside the edited range instead of reparsing from
    // scratch. Must be called (once per edit, in order) before that call; a
    // no-op if IsNull().
    void Edit(const parse::InputEdit& edit) noexcept;

    // The underlying green tree, for Parser::Parse's old-tree incremental
    // overload and code in this directory. Not for use outside
    // Source/Editor/TreeSitter/.
    [[nodiscard]] const parse::GreenTree& Green() const noexcept;

  private:
    parse::GreenTree tree_;
};

} // namespace ned::editor::treesitter

#endif // NED_EDITOR_TREESITTER_TREE_H
