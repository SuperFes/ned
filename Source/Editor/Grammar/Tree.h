//
// The parse-tree handle the editor holds -- since the Phase 4b engine swap
// a thin wrapper over ned's own parse::GreenTree rather than a TSTree*.
// See Node.h's header comment for the overall wrapper rationale.
//
// Kept move-only even though GreenTree itself is cheaply copyable: every
// existing consumer was written against the original unique-handle
// semantics, and keeping them is what makes the swap invisible.
//

#ifndef NED_EDITOR_GRAMMAR_TREE_H
#define NED_EDITOR_GRAMMAR_TREE_H

#include "Editor/Parse/Tree.h"

#include "Node.h"

namespace ned::editor::grammar {

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
    // Source/Editor/Grammar/.
    [[nodiscard]] const parse::GreenTree& Green() const noexcept;

    // per-subtree-fact-memoization follow-up: an independent second handle
    // on the SAME green tree (parse::GreenTree's shared_ptr<TreeData> copy
    // -- cheap, no reparse, no deep copy of any Subtree). Needed because
    // Node::SubtreeIdentity's "same heap pointer -> same content" guarantee
    // only holds for a subtree tree-sitter's reuse machinery actually
    // decided to SHARE, which depends on nothing else holding it unshared
    // (Green.h: "immutable once shared (refcount > 1); MakeMut clones on
    // sharing" -- an unshared subtree along the touched spine of an edit is
    // freely mutated/recycled at its OLD address instead, since nothing
    // could have observed the difference). IncrementalParseCache's own
    // Update() mutates its single held Tree via Edit()+reassignment with no
    // separate old-generation handle in between, so by itself it gives NO
    // such guarantee across a call -- a caller that wants to compare node
    // identity between two generations (a per-subtree fact cache) MUST take
    // a Clone() of "before" and keep it alive across the Update() call that
    // produces "after"; only then does reuse-vs-recycle become
    // distinguishable via identity, because a genuinely shared subtree's
    // refcount is >1 for the whole call and MakeMut clones instead.
    [[nodiscard]] Tree Clone() const;

  private:
    parse::GreenTree tree_;
};

} // namespace ned::editor::grammar

#endif // NED_EDITOR_GRAMMAR_TREE_H
