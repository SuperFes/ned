//
// call/type-hierarchy follow-up. A generic expandable-tree picker widget --
// this codebase's first (every existing LSP-result view, code-action
// select, symbol pickers, references, is a flat ListPopup-shaped list).
// Mirrors ListPopup's own contract deliberately: a pure renderer over
// repeated SetModel() calls, with no tree state of its own -- the caller
// (BufferView, driving an Editor/ExpandableTree.h instance) owns
// expand/collapse/loading policy and pushes a freshly flattened model after
// every change. Kept data-agnostic (a plain label string per row, not an
// LSP-shaped struct) so a future non-LSP tree-shaped feature (a VCS log
// graph, a project outline pane) can reuse this instead of growing its own
// -- see ROADMAP.md's own call/type-hierarchy entry.
//

#ifndef NED_UI_TREEVIEW_H
#define NED_UI_TREEVIEW_H

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "Theme.h"
#include "Widget.h"

namespace ned::ui {

struct TreeRow {
    std::string label;
    std::size_t depth = 0; // 0 for a root -- indentation is depth * 2 columns

    // Whether to show an expand affordance at all. True both for "not yet
    // asked" (the common case: a hierarchy item's children are unknown
    // until fetched, so every unexpanded row defaults to assuming it might
    // have some) and for "asked, and it really does have children" --
    // false only once a fetch has confirmed there are none (a real leaf).
    // Mirrors Editor/ExpandableTree.h's own !ChildrenFetched(i) ||
    // !children.empty() derivation.
    bool hasChildren = true;
    bool expanded    = false; // only meaningful when hasChildren
    bool loading     = false; // an expand request is in flight for this row
};

struct TreeViewModel {
    std::string                title;
    std::vector<TreeRow>       rows; // already-flattened, pre-order, visible only
    std::optional<std::size_t> selectedIndex;
};

class TreeView : public Widget {
  public:
    explicit TreeView(const Theme& theme);

    // Replaces the displayed content. Does not show/hide the widget itself
    // -- that's the caller's job via OverlayHost::Show/Hide, matching
    // ListPopup's own split of concerns.
    void SetModel(TreeViewModel model);

    [[nodiscard]] bool Focusable() const override {
        return true;
    }

    // Fires whenever Up/Down changes the selected row (never on a no-op
    // move against a single-row or empty model). Unlike ListPopup, this
    // widget owns its own selection between SetModel calls (it takes real
    // keyboard focus and drives Up/Down itself) -- a caller that must
    // rebuild the whole model on every expand/collapse (the call/type
    // hierarchy browser, inserting or removing rows) needs this to carry
    // the current selection forward into the rebuilt model, or every
    // rebuild would silently reset it.
    void SetOnSelectionChanged(std::function<void(std::size_t)> onSelectionChanged);

    // Enter, or a mouse click on a row: "open/jump to this row," regardless
    // of hasChildren/expanded state.
    void SetOnActivate(std::function<void(std::size_t)> onActivate);

    // Right-arrow on a row with hasChildren && !expanded && !loading. The
    // caller decides whether this needs a real fetch (ExpandableTree::
    // ChildrenFetched(index) false) or can just reveal already-fetched
    // children (SetExpanded(index, true)) -- this widget has no opinion,
    // it only reports "the user asked to open this node."
    void SetOnToggleExpand(std::function<void(std::size_t)> onToggleExpand);

    // Left-arrow on a row with hasChildren && expanded. Purely a visibility
    // change from the caller's own ExpandableTree's perspective (no
    // re-fetch), but since this widget holds no tree state itself the
    // caller must still handle this and push an updated model.
    void SetOnCollapseRequested(std::function<void(std::size_t)> onCollapseRequested);

    void SetOnCancel(std::function<void()> onCancel);

    void Paint(Canvas c) override;
    bool OnEvent(const Event& event) override;

  private:
    const Theme&   theme_;
    TreeViewModel  model_;

    std::function<void(std::size_t)> onSelectionChanged_;
    std::function<void(std::size_t)> onActivate_;
    std::function<void(std::size_t)> onToggleExpand_;
    std::function<void(std::size_t)> onCollapseRequested_;
    std::function<void()>            onCancel_;

    bool HandleKeyEvent(const Event& event);
    bool HandleMouseEvent(const Event& event);
};

} // namespace ned::ui

#endif // NED_UI_TREEVIEW_H
