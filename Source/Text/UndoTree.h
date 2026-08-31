//
// Per-buffer undo history as an actual tree, not Emacs' flat
// undo-as-an-editable-list. Undo moves to the parent; redo moves to the
// most-recently-visited child. A new edit made after undoing creates a
// sibling branch rather than discarding what was undone away from, so no
// history is ever lost -- Emacs' core guarantee -- without Emacs' "redo by
// undoing the undo" surprise.
//
// Each node stores a full storage snapshot (ITextStorage, not a diff).
// That's cheap because both concrete storage kinds (Rope, PieceTable) are
// persistent/structurally-shared: creating a snapshot after a small edit
// only allocates the O(log n) nodes along the edited path, sharing
// everything else with the previous snapshot.
//

#ifndef NED_TEXT_UNDOTREE_H
#define NED_TEXT_UNDOTREE_H

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "ITextStorage.h"

namespace ned::text {

class UndoTree {
  public:
    explicit UndoTree(std::unique_ptr<ITextStorage> initialState);

    [[nodiscard]] const ITextStorage& Current() const;

    // Creates a new undo step as a child of the current node and moves onto it.
    void Record(std::unique_ptr<ITextStorage> newState);

    // Replaces the current node's state in place instead of creating a new
    // step. Used by callers to coalesce a run of adjacent edits (e.g.
    // consecutive character inserts while typing) into a single undo step;
    // the tree itself has no opinion on when that's appropriate.
    void Amend(std::unique_ptr<ITextStorage> newState);

    [[nodiscard]] bool CanUndo() const;
    [[nodiscard]] bool CanRedo() const;

    void Undo();
    void Redo();

    // Number of branches recorded at the current node (i.e. how many distinct
    // edits have followed this point in history across all undo/redo/branch
    // activity). Exposed mainly so callers/tests can confirm branching
    // doesn't discard anything.
    [[nodiscard]] std::size_t ChildCount() const;

    // persistent-undo follow-up: a flat, JSON-agnostic view of the whole
    // tree (not just the current-to-root path) for a caller one layer up
    // (Editor/PersistentUndo.h) to serialize to disk -- this class stays
    // free of any JSON/file dependency, matching every other Text/ type.
    // id is a pre-order-DFS index (deterministic given a fixed tree, so it's
    // stable across repeated Serialize() calls on the same instance, but not
    // meant to persist meaning beyond one Serialize()/Deserialize() round
    // trip). parentId is nullopt only for the root. mostRecentChild mirrors
    // Node::mostRecentChild -- which of this node's own children Redo()
    // would follow -- 0 (meaningless) when the node has no children.
    //
    // content is always a plain-text snapshot (ITextStorage::ToString()),
    // regardless of which concrete storage kind produced it -- this is only
    // ever reachable for a Rope-backed buffer in practice: Editor/
    // PersistentUndo.h gates on PersistentUndoMaxSizeMb (default 16 MiB)
    // well below where a huge/piece-table-backed buffer would ever exist,
    // so Serialize() never actually runs against one, even though it would
    // compile and technically work (just slowly) if it did.
    struct SerializedNode {
        std::size_t                id;
        std::optional<std::size_t> parentId;
        std::string                content;
        std::size_t                mostRecentChild = 0;
    };
    [[nodiscard]] std::vector<SerializedNode> Serialize() const;
    // The id (per the same Serialize() walk) of the node Current() reflects.
    [[nodiscard]] std::size_t CurrentNodeId() const;
    // Reconstructs a tree from a flat node list (order-independent -- each
    // node is placed by parentId, not by its position in nodes) plus which
    // node is current. Every node must be reachable from the one root
    // (parentId == nullopt); throws std::runtime_error on a malformed list
    // (no root, more than one root, a dangling parentId, an unknown
    // currentId, or a cycle) -- the caller (a hand-edited or corrupted undo
    // file) can't be trusted to hand back exactly what Serialize() produced.
    // Every reconstructed node is Rope-backed (see SerializedNode's own doc
    // comment above on why that's always correct here).
    [[nodiscard]] static UndoTree Deserialize(const std::vector<SerializedNode>& nodes, std::size_t currentId);

  private:
    struct Node {
        std::unique_ptr<ITextStorage>       state;
        Node*                                parent = nullptr; // non-owning; root_ keeps the tree alive
        std::vector<std::unique_ptr<Node>>  children;
        std::size_t                          mostRecentChild = 0;
    };

    std::unique_ptr<Node> root_;
    Node*                 current_ = nullptr; // non-owning observer into root_'s tree
};

} // namespace ned::text

#endif // NED_TEXT_UNDOTREE_H
