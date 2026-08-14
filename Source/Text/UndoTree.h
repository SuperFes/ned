//
// Per-buffer undo history as an actual tree, not Emacs' flat
// undo-as-an-editable-list. Undo moves to the parent; redo moves to the
// most-recently-visited child. A new edit made after undoing creates a
// sibling branch rather than discarding what was undone away from, so no
// history is ever lost -- Emacs' core guarantee -- without Emacs' "redo by
// undoing the undo" surprise.
//
// Each node stores a full Rope snapshot rather than a diff. That's cheap
// because Rope is a persistent/structurally-shared data structure: creating a
// snapshot after a small edit only allocates the O(log n) nodes along the
// edited path, sharing everything else with the previous snapshot.
//

#ifndef NED_TEXT_UNDOTREE_H
#define NED_TEXT_UNDOTREE_H

#include <cstddef>
#include <memory>
#include <vector>

#include "Rope.h"

namespace ned::text {

class UndoTree {
  public:
    explicit UndoTree(Rope initialState);

    [[nodiscard]] const Rope& Current() const;

    // Creates a new undo step as a child of the current node and moves onto it.
    void Record(Rope newState);

    // Replaces the current node's state in place instead of creating a new
    // step. Used by callers to coalesce a run of adjacent edits (e.g.
    // consecutive character inserts while typing) into a single undo step;
    // the tree itself has no opinion on when that's appropriate.
    void Amend(Rope newState);

    [[nodiscard]] bool CanUndo() const;
    [[nodiscard]] bool CanRedo() const;

    void Undo();
    void Redo();

    // Number of branches recorded at the current node (i.e. how many distinct
    // edits have followed this point in history across all undo/redo/branch
    // activity). Exposed mainly so callers/tests can confirm branching
    // doesn't discard anything.
    [[nodiscard]] std::size_t ChildCount() const;

  private:
    struct Node {
        Rope                                state;
        Node*                                parent = nullptr; // non-owning; root_ keeps the tree alive
        std::vector<std::unique_ptr<Node>>  children;
        std::size_t                          mostRecentChild = 0;
    };

    std::unique_ptr<Node> root_;
    Node*                 current_ = nullptr; // non-owning observer into root_'s tree
};

} // namespace ned::text

#endif // NED_TEXT_UNDOTREE_H
