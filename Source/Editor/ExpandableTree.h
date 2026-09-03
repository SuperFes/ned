//
// call/type-hierarchy follow-up. A generic, UI-free, unit-tested lazy tree:
// unlike CodeFold's FoldMarker (which folds an already-fully-known
// structure) or Org's outline (parsed wholesale from buffer text), a
// hierarchy's children are unknown until asked for -- each expand is its own
// async LSP round trip. Mirrors this codebase's existing "pure state
// machine, driven externally by BufferView" precedent (PrefixArgumentReader,
// IncrementalSearch): this file owns expand/collapse/loading policy only,
// with no rendering opinion at all -- Source/UI/TreeView (a follow-up) is a
// dumb renderer over FlattenVisible()'s output.
//
// Templated on NodeData rather than hardcoded to
// ned::editor::lsp::HierarchyItem so a future non-LSP tree-shaped feature
// (a VCS log graph, a project outline pane -- see ROADMAP.md's own call/
// type-hierarchy entry) can reuse this instead of growing its own; Editor/
// must not depend on any one subsystem's vocabulary this generically.
//
// Node identity is a stable index into an internally-growing vector: nodes
// are appended by Expand() and never removed (Collapse only hides a
// subtree, it doesn't discard the fetched children -- re-expanding is then
// free, no re-fetch), so an index handed back to a caller stays valid for
// the lifetime of the tree.
//

#ifndef NED_EDITOR_EXPANDABLETREE_H
#define NED_EDITOR_EXPANDABLETREE_H

#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

namespace ned::editor {

template <typename NodeData>
class ExpandableTree {
  public:
    struct Node {
        NodeData                    data;
        std::optional<std::size_t>  parent; // nullopt for a root
        std::vector<std::size_t>    children;
        bool                        expanded        = false;
        bool                        loading         = false; // an Expand() request is in flight
        bool                        childrenFetched = false; // Expand() has run at least once -- distinguishes "never asked" from "asked, server said none"
    };

    // Discards any existing nodes and seeds new roots (e.g. a fresh
    // prepareCallHierarchy result) -- a whole new hierarchy session, not an
    // incremental update.
    void Reset(std::vector<NodeData> roots) {
        nodes_.clear();
        nodes_.reserve(roots.size());
        for (NodeData& root : roots) {
            nodes_.push_back(Node{.data = std::move(root), .parent = std::nullopt});
        }
    }

    [[nodiscard]] std::size_t Size() const {
        return nodes_.size();
    }

    [[nodiscard]] const Node& At(std::size_t index) const {
        return nodes_[index];
    }

    [[nodiscard]] bool IsExpanded(std::size_t index) const {
        return index < nodes_.size() && nodes_[index].expanded;
    }

    [[nodiscard]] bool IsLoading(std::size_t index) const {
        return index < nodes_.size() && nodes_[index].loading;
    }

    [[nodiscard]] bool ChildrenFetched(std::size_t index) const {
        return index < nodes_.size() && nodes_[index].childrenFetched;
    }

    // Marks a node as awaiting its children -- set before issuing the async
    // request, so a FlattenVisible() row painted before the response arrives
    // can show a loading affordance.
    void BeginLoading(std::size_t index) {
        if (index < nodes_.size()) {
            nodes_[index].loading = true;
        }
    }

    // Called once an async children request completes: appends one new node
    // per child (parented to index) and marks index expanded/fetched/no
    // longer loading. Precondition: !ChildrenFetched(index) -- this appends
    // unconditionally, so calling it twice for the same node would duplicate
    // its children; a caller that already has fetched children for a node
    // should use SetExpanded instead of re-fetching. childData empty still
    // marks childrenFetched=true (a real "no callers/no subtypes" answer,
    // distinct from "not asked yet").
    void Expand(std::size_t index, std::vector<NodeData> childData) {
        if (index >= nodes_.size()) {
            return;
        }
        nodes_[index].loading         = false;
        nodes_[index].expanded        = true;
        nodes_[index].childrenFetched = true;
        nodes_[index].children.reserve(childData.size());
        for (NodeData& child : childData) {
            nodes_.push_back(Node{.data = std::move(child), .parent = index});
            nodes_[index].children.push_back(nodes_.size() - 1);
        }
    }

    // Purely local visibility toggle over already-fetched children -- no
    // network round trip either way. A no-op setting true on a node with no
    // fetched children yet (nothing to reveal); the caller is expected to
    // check ChildrenFetched itself to decide between this and
    // BeginLoading+Expand.
    void SetExpanded(std::size_t index, bool expanded) {
        if (index < nodes_.size()) {
            nodes_[index].expanded = expanded;
        }
    }

    struct VisibleRow {
        std::size_t index;
        std::size_t depth; // 0 for a root

        bool operator==(const VisibleRow&) const = default;
    };

    // Pre-order flatten of every currently visible node: every root, then
    // each expanded node's children recursively -- a collapsed subtree's
    // descendants are omitted entirely, the same "folded means not walked"
    // semantics CodeFold's FoldedLineRanges already uses. Roots are visited
    // in Reset's own insertion order.
    [[nodiscard]] std::vector<VisibleRow> FlattenVisible() const {
        std::vector<VisibleRow> rows;
        for (std::size_t i = 0; i < nodes_.size(); ++i) {
            if (!nodes_[i].parent) {
                AppendVisible(i, 0, rows);
            }
        }
        return rows;
    }

  private:
    void AppendVisible(std::size_t index, std::size_t depth, std::vector<VisibleRow>& rows) const {
        rows.push_back(VisibleRow{index, depth});
        if (nodes_[index].expanded) {
            for (std::size_t child : nodes_[index].children) {
                AppendVisible(child, depth + 1, rows);
            }
        }
    }

    std::vector<Node> nodes_;
};

} // namespace ned::editor

#endif // NED_EDITOR_EXPANDABLETREE_H
