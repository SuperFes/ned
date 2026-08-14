//
// A persistent left-side project tree (project-sidebar follow-up, the
// user's own "Dired-like file browser" idea): lists files/directories under
// the current working directory, click a file to open it, click a directory
// to expand/collapse it. Mouse-only (FocusPolicy::None, same as
// TabBar/ScrollArrowButton) -- clicking never steals keyboard focus from
// BufferView.
//
// A single click on a file opens it as a transient *preview* (single-click-
// preview follow-up, VS Code-style): reuses an already-open buffer for that
// path if one exists (BufferList::FindByPath), otherwise opens a new one and
// marks it BufferList::SetPreviewBuffer -- closing whatever the previous
// preview was first, so previews replace rather than accumulate (see
// BufferList::PreviewBuffer's own doc comment for the full promotion
// story). A double click (tracked here via a simple same-path-within-
// kDoubleClickWindow timer -- TermOx has no built-in double-click
// detection) opens/promotes the buffer as a real, permanent one directly,
// no preview involved.
//
// Directories start collapsed (round-2 follow-up feedback reversed the
// original "always fully expanded" v1 design) -- expandedDirs_ tracks which
// ones the user has opened, as view-only state recomputed against a fresh
// BuildProjectTree() walk on every paint()/mouse_press()/mouse_wheel() call
// rather than being baked into ProjectTree itself, which stays a plain,
// stateless directory walk.
//
// While scrolled into nested content, ancestor directory rows that would
// otherwise have scrolled off the top stay pinned there instead ("sticky
// scroll", VS Code-style) -- see ComputeRowLayout/AncestorIndices in the
// .cpp. No keyboard interaction is still a v1 scope cut, not an oversight;
// see ROADMAP.md. SidebarToggle (a separate, always-visible sibling widget
// -- see its own header comment) is the mouse-clickable way to hide/show
// this widget; C-c C-p (toggle-project-sidebar) does the same thing from
// the keyboard.
//
// The width is drag-resizable by the divider column (round-2 follow-up):
// pressing on it starts a resize session (IsResizing()/UpdateResize()/
// EndResize()), which BufferView cooperates in -- TermOx has no
// mouse-capture concept (every event, including move and release, is
// independently position-hit-tested against whatever's under the cursor at
// that moment), so once a growing drag crosses out of this widget's own
// bounds the move events land on BufferView instead. BufferView checks
// IsResizing() in its own mouse_move/mouse_release and, if set, drives the
// resize instead of its usual selection/no-op handling. See UpdateResize's
// own comment for why the math is anchored to the drag's start rather than
// applied as a per-event delta.
//

#ifndef NED_UI_PROJECTSIDEBAR_H
#define NED_UI_PROJECTSIDEBAR_H

#include <chrono>
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <ox/ox.hpp>

#include "ActiveBuffer.h"
#include "Editor/ProjectTree.h"
#include "Text/BufferList.h"
#include "Theme.h"

namespace ned::ui {

class ProjectSidebar : public ox::Widget {
  public:
    // activeBuffer, bufferList, statusMessage, and theme must outlive this
    // widget (the usual convention). statusMessage is where a failed file
    // open gets reported (e.g. permission denied) -- the one operation here
    // that can actually fail, unlike TabBar's clicks, which only ever
    // switch between already-open buffers.
    ProjectSidebar(ActiveBuffer& activeBuffer, text::BufferList& bufferList, std::string& statusMessage,
                   const Theme& theme);

    void paint(ox::Canvas c) override;
    void mouse_press(ox::Mouse mouse) override;
    void mouse_move(ox::Mouse mouse) override;
    void mouse_release(ox::Mouse mouse) override;
    void mouse_wheel(ox::Mouse mouse) override;

    // Registers the ox::Row containing this widget so a resize-drag can
    // force it to immediately reflow (plain SizePolicy/at/size field writes
    // don't trigger a relayout on their own -- see
    // BufferView::SetSidebarRow's longer comment for the full explanation).
    // nullptr (the default) means dragging the divider changes size_policy
    // but never visibly reflows until the next real terminal resize.
    void SetSidebarRow(ox::Widget* sidebarRow);

    [[nodiscard]] bool IsResizing() const;

    // Called by BufferView's own mouse_move while IsResizing() -- see this
    // header's comment above for why BufferView needs to be involved at
    // all. globalMouseX is the caller's own mouse.at.x plus its own at.x
    // (i.e. the mouse position in the containing Row's coordinate space,
    // not the caller's local one), so this widget can compare it against
    // the drag's remembered starting position regardless of which widget
    // the event actually landed on.
    void UpdateResize(int globalMouseX);

    // Called by whichever widget's mouse_release lands during a resize
    // (this widget's own, or BufferView's) to end the session.
    void EndResize();

    // Expands every ancestor directory (project-root-detection follow-up)
    // between the current ProjectRoot() and targetPath's own containing
    // directory, so the file is actually reachable in the tree instead of
    // hidden behind a collapsed ancestor -- called once from main.cpp right
    // after startup with whatever file was opened, since a VCS-detected
    // root can easily put real distance between the tree's top and the
    // file the user is actually looking at. A safe no-op if targetPath
    // doesn't fall under the current root at all. Does not itself scroll
    // the newly-revealed row into view -- this runs before the widget has
    // ever been laid out, so there's no real size().height yet to scroll
    // against; if the file wasn't near the top of the tree to begin with,
    // the user may still need to scroll to actually see the now-expanded
    // path down to it. An explicit, narrow v1 scope cut, not an oversight.
    void RevealPath(const std::filesystem::path& targetPath);

  private:
    ActiveBuffer&     activeBuffer_;
    text::BufferList& bufferList_;
    std::string&      statusMessage_;
    const Theme&      theme_;

    int scrollOffset_ = 0; // first visible row (post-sticky-headers), in *visible* (post-collapse) tree-entry units

    // Directories the user has expanded, by absolute path. A directory not
    // in this set (which is every directory, initially) renders collapsed:
    // listed itself, but its own children filtered out of the visible list.
    std::set<std::filesystem::path> expandedDirs_;

    ox::Widget* sidebarRow_ = nullptr; // see SetSidebarRow

    bool resizing_            = false;
    int  resizeAnchorGlobalX_ = 0; // this Row-space mouse x when the drag started
    int  resizeAnchorWidth_   = 0; // this widget's own width when the drag started

    void BeginResize(int globalMouseX);

    // Double-click detection for file rows (single-click-preview follow-up):
    // a second click on the *same path* within kDoubleClickWindow counts as
    // a double click. Directory clicks (expand/collapse) don't go through
    // this at all -- only file opens care about single vs. double.
    std::optional<std::filesystem::path>  lastFileClickPath_;
    std::chrono::steady_clock::time_point lastFileClickTime_;

    void OpenFileEntry(const std::filesystem::path& path, bool isDoubleClick);

    [[nodiscard]] std::vector<editor::ProjectTreeEntry> VisibleEntries(
        const std::vector<editor::ProjectTreeEntry>& all) const;
};

} // namespace ned::ui

#endif // NED_UI_PROJECTSIDEBAR_H
