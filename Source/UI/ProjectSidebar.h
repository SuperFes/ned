//
// A persistent left-side project tree (project-sidebar follow-up, the
// user's own "Dired-like file browser" idea): lists files/directories under
// the current working directory, click a file to open it, click a directory
// to expand/collapse it. Mouse-only (no keyboard focus, same as
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
// kDoubleClickWindow timer) opens/promotes the buffer as a real, permanent
// one directly, no preview involved.
//
// Directories start collapsed (round-2 follow-up feedback reversed the
// original "always fully expanded" v1 design) -- expandedDirs_ tracks which
// ones the user has opened, as view-only state recomputed against a fresh
// BuildProjectTree() walk on every Paint()/OnEvent() call rather than being
// baked into ProjectTree itself, which stays a plain, stateless directory
// walk.
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
// EndResize()), which BufferView cooperates in -- FTXUI has no
// mouse-capture concept either (every mouse event is delivered to every
// leaf widget regardless of position; see Widget.h's own header comment),
// so once a growing drag crosses out of this widget's own bounds the move
// events land on BufferView too. BufferView checks IsResizing() in its own
// OnEvent and, if set, drives the resize instead of its usual selection/
// no-op handling. See UpdateResize's own comment for why the math is
// anchored to the drag's start rather than applied as a per-event delta.
// Width() is read by main.cpp's own composition-root Renderer every frame
// to decide this widget's actual layout width (TermOx -> FTXUI migration:
// there's no equivalent of directly mutating a stored `size_policy` field
// here anymore, since FTXUI rebuilds its whole Element tree -- including
// every size()/flex() decorator -- fresh every frame from whatever the
// composition root's own render function returns).
//

#ifndef NED_UI_PROJECTSIDEBAR_H
#define NED_UI_PROJECTSIDEBAR_H

#include <chrono>
#include <filesystem>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "ActiveBuffer.h"
#include "Editor/ProjectTree.h"
#include "Text/BufferList.h"
#include "Theme.h"
#include "Widget.h"

namespace ned::ui {

class ProjectSidebar : public Widget {
  public:
    // activeBufferProvider, bufferList, statusMessage, and theme must
    // outlive this widget (the usual convention). statusMessage is where a
    // failed file open gets reported (e.g. permission denied) -- the one
    // operation here that can actually fail, unlike TabBar's clicks, which
    // only ever switch between already-open buffers. activeBufferProvider
    // (window-splitting follow-up; was a fixed ActiveBuffer&) is called
    // fresh on every click rather than bound once at construction, for the
    // exact same reason TabBar's own provider exists: a sidebar click
    // should always open into whichever pane currently has keyboard focus,
    // which changes over time. main.cpp wires this to
    // WindowManager::FocusedActiveBuffer.
    ProjectSidebar(std::function<ActiveBuffer&()> activeBufferProvider, text::BufferList& bufferList,
                   std::string& statusMessage, const Theme& theme);

    void Paint(Canvas c) override;
    bool OnEvent(const Event& event) override;

    // Current desired width in columns -- see this file's own header
    // comment for why main.cpp's composition root reads this every frame
    // rather than this widget mutating a stored layout policy directly.
    // Starts at initialWidth (main.cpp passes the same 30 the pre-migration
    // version's initial fixed(30) used); UpdateResize below is the only
    // thing that ever changes it afterward.
    [[nodiscard]] int Width() const;

    [[nodiscard]] bool IsResizing() const;

    // Called by BufferView's own OnEvent while IsResizing() -- see this
    // header's comment above for why BufferView needs to be involved at
    // all. globalMouseX is the raw, absolute (screen-space) mouse x
    // FTXUI's own Mouse::x already is -- unlike the pre-migration version,
    // no translation from a caller-local coordinate is needed (FTXUI never
    // translates mouse coordinates to begin with; see Widget.h's own header
    // comment), so this is just whichever widget's OnEvent received the
    // event passing its raw event.mouse().x straight through.
    void UpdateResize(int globalMouseX);

    // Called by whichever widget's OnEvent sees the matching mouse-release
    // during a resize (this widget's own, or BufferView's) to end the
    // session.
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

    // Forces the next CachedTree() call to rebuild from disk immediately,
    // bypassing the usual throttle window (see CachedTree()'s own comment).
    // Called by BufferView after any of its own operations that change the
    // tree's shape (create-directory, delete-file, rename-file) so the
    // sidebar reflects the app's own actions right away rather than waiting
    // out the throttle -- a no-op cost-wise if this ProjectSidebar isn't
    // even wired up to a live BufferView (e.g. most unit tests), since
    // nothing calls it there.
    void InvalidateTree();

    // Window-splitting follow-up: called with the outgoing single-click-
    // preview buffer immediately *before* OpenFileEntry closes it directly
    // (bufferList_.Close(...) -- this widget's own preview-replacement
    // logic, unrelated to BufferView::CloseBufferNow/SetOnBufferClosed
    // entirely). Unset (the default) is a safe no-op, matching every other
    // Set* hook in this codebase -- but leaving it unset while multiple
    // panes exist is a real, confirmed bug: any *other* pane that happened
    // to also be showing the outgoing preview is left with a dangling
    // ActiveBuffer the instant bufferList_.Close() actually frees it,
    // crashing the next time that pane repaints (confirmed via two real
    // coredumps -- heap corruption manifesting inside ModeLine::Paint's own
    // string building -- not a hypothetical). main.cpp wires this to
    // WindowManager::NotifyBufferClosing.
    void SetOnBufferClosed(std::function<void(text::Buffer&)> handler);

  private:
    std::function<ActiveBuffer&()>     activeBufferProvider_;
    text::BufferList&                  bufferList_;
    std::string&                       statusMessage_;
    const Theme&                       theme_;
    std::function<void(text::Buffer&)> onBufferClosed_;

    int scrollOffset_ = 0; // first visible row (post-sticky-headers), in *visible* (post-collapse) tree-entry units

    // Directories the user has expanded, by absolute path. A directory not
    // in this set (which is every directory, initially) renders collapsed:
    // listed itself, but its own children filtered out of the visible list.
    std::set<std::filesystem::path> expandedDirs_;

    int width_ = 30; // see Width()

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

    // sidebar-header follow-up: row 0 is always the project-name header
    // (see Paint()'s own comment), never tree content -- every tree-row
    // computation (ComputeRowLayout/EntryIndexAtRow's viewportHeight, the
    // wheel-scroll clamp) works in this content-only height, not
    // size().height directly, and every row/y value crossing that boundary
    // gets shifted by kHeaderHeight exactly once, at the call site.
    [[nodiscard]] int ContentHeight() const;

    // BuildProjectTree does a full recursive directory walk -- cheap for a
    // small project, genuinely expensive (tens of milliseconds, measured)
    // for a large one, and this widget's Paint()/OnEvent() used to call it
    // unconditionally on every single call. Under FTXUI that means every
    // single frame -- i.e. every keystroke, even ones with nothing to do
    // with the sidebar at all, since FTXUI repaints the whole component
    // tree fresh each frame -- a real, reported, felt typing/cursor lag,
    // not a hypothetical one. CachedTree() rebuilds at most once per
    // kTreeCacheThrottle, or immediately if ProjectRoot() changed or
    // InvalidateTree() was called since the last build; every other frame
    // reuses the cached result. A bounded, deliberately simple fix (no file-
    // system watcher) -- external changes (e.g. `git checkout` in another
    // terminal) can lag up to kTreeCacheThrottle behind, an accepted
    // trade-off for a project-tree sidebar, not a correctness requirement.
    [[nodiscard]] const std::vector<editor::ProjectTreeEntry>& CachedTree();

    std::vector<editor::ProjectTreeEntry> treeCache_;
    bool                                  treeCacheValid_ = false;
    std::filesystem::path                 treeCacheRoot_;
    std::chrono::steady_clock::time_point treeCacheTime_;
};

} // namespace ned::ui

#endif // NED_UI_PROJECTSIDEBAR_H
