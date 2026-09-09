//
// A persistent left-side project tree (project-sidebar follow-up, the
// user's own "Dired-like file browser" idea): lists files/directories under
// the current working directory, click a file to open it, click a directory
// to expand/collapse it. click-to-focus follow-up: a real mouse press
// anywhere in this widget takes keyboard focus (BufferView::OnMouseEvent's
// own convention), reversing this widget's original "mouse-only, clicking
// never steals keyboard focus from BufferView" design -- C-c p/
// focus-project-sidebar remains the keyboard-only entry point (see
// SetOnFocusReturn's own doc comment for the LeftDock collapse/expand
// pairing that path still drives).
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
// see ROADMAP.md.
//
// unified-left-dock follow-up (migration step 2): this widget no longer
// owns a border, width, or collapse-to-a-strip state of its own -- it's now
// a plain content view hosted inside LeftDock (Source/UI/LeftDock.h), which
// owns the frame/width/collapse/resize-drag divider for the left dock slot
// this widget fills. LeftDock hands this widget a Canvas already scoped to
// its own interior box (border excluded), calling Paint()/OnEvent() only
// while its panel is the active one and the dock isn't collapsed -- when
// it's not, this widget simply isn't painted or forwarded events, so it
// needs no collapsed-state branch of its own.
//
// Row 0 of whatever canvas this widget IS handed stays its own header --
// the live project name, click-to-switch-project (SetOnHeaderClicked) --
// since LeftDock's own border title is a fixed per-panel label ("Files"),
// not the dynamic project name; that's a real per-panel feature LeftDock's
// generic chrome has no way to express, so it stays here as an ordinary
// content row instead of a border title. Tree content fills every row
// below it.
//

#ifndef NED_UI_PROJECTSIDEBAR_H
#define NED_UI_PROJECTSIDEBAR_H

#include <chrono>
#include <filesystem>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "ActiveBuffer.h"
#include "Editor/Project/Tree.h"
#include "Editor/Vcs/RowStatus.h"
#include "Editor/Vcs/Runner.h"
#include "Text/BufferList.h"
#include "Theme.h"
#include "Widget.h"

namespace ned::ui {

// changed-files-highlight follow-up: RowStatus (how severe a row's git
// status is) now lives in Editor/Vcs/RowStatus.h, shared with VcsPanel
// (VCS side panel follow-up) rather than defined here alone.
using editor::vcs::RowStatus;

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

    // sidebar-keyboard-focus follow-up: focus-project-sidebar (C-c p) hands
    // this widget the keyboard via Widget::TakeFocus; while Focused(), key
    // events drive a selection cursor (Up/Down or C-p/C-n, Enter to
    // open/toggle, Left/Right to collapse/expand a directory, Escape/C-g to
    // return focus) and the frame paints in the accent brush -- the same
    // "this has your attention" signal a resize drag already gives.
    [[nodiscard]] bool Focusable() const override {
        return true;
    }

    // unified-left-dock follow-up: LeftDock collapsing while this widget
    // holds focus (keyboard toggle or a rail-glyph mouse click, see
    // Widget::OnFocusPreempted's own doc comment) hands focus back the same
    // way Escape/C-g does.
    void OnFocusPreempted() override {
        ReturnFocus();
    }

    // Called when keyboard focus should go back to the editor (Escape/C-g,
    // or after Enter opens a file). Unset (the default) is a safe no-op,
    // matching every other Set* hook here; main.cpp wires this to
    // WindowManager::TakeFocus.
    void SetOnFocusReturn(std::function<void()> handler);

    // named-projects follow-up: fired on a click anywhere in the header/
    // title row -- OnEvent already reserves and no-ops that row specifically
    // for this (see its own comment), so wiring it up here needed no
    // hit-testing changes at all. Unset (the default) is a safe no-op,
    // matching every other Set* hook here; main.cpp wires this to fire
    // switch-project the same way a keybinding would.
    void SetOnHeaderClicked(std::function<void()> handler);

    // project-sidebar-drag-drop follow-up: same cross-widget cooperation
    // shape LeftDock::IsResizing()/EndResize() use for its own resize-drag
    // handoff -- a left-press on a file row (never a directory; there's no
    // single sensible target to open) additionally arms this alongside the
    // row's existing open-preview behavior. BufferView checks it in its own
    // OnMouseEvent (rawMouse, ahead of that widget's own LocalMouseEvent
    // gate, mirroring how it already checks LeftDock::IsResizing()) and,
    // when a Released event lands inside
    // its own Box_(), opens the dragged file there and calls EndFileDrag()
    // itself. A Released landing back on this widget's own bounds is just
    // an ordinary click (already handled by the press-time open) and clears
    // this with no drop action of its own. A Released claimed by neither --
    // dropped on the tab bar, VCS panel, or echo area -- leaves this armed
    // until the next drag start overwrites it; a documented, harmless v1
    // edge case, since nothing else ever reads it.
    [[nodiscard]] std::optional<std::filesystem::path> DraggingFilePath() const;
    void                                               EndFileDrag();

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

    // open-binary-anyway follow-up: called instead of just reporting
    // text::BinaryFileError via statusMessage_ (this widget's own generic
    // catch-all fallback, still used for every other kind of open failure)
    // -- this widget is mouse-only and can't itself drive a keyboard y/n
    // confirmation (see this file's own header comment), so it hands off
    // to whichever pane's BufferView can. Unset (the default) falls back
    // to the plain refusal message, same as before this follow-up.
    // main.cpp wires this to WindowManager::RequestOpenBinaryFile.
    void SetOnBinaryFileOpenRequest(std::function<void(const std::filesystem::path&)> handler);

    // changed-files-highlight follow-up: unset (the default, matching every
    // other Set* hook here) leaves every row rendered exactly as before --
    // no VCS provider ever resolving for the root behaves the same way
    // (Runner::RequestStatus's onError just fires, vcsStatus_ stays
    // empty). main.cpp wires this the same place it wires
    // WindowManager::SetVcsRunner.
    void SetVcsRunner(editor::vcs::Runner* vcsRunner);

    // Testing-only entry point, BufferView::DispatchStatusForTesting's own
    // precedent: builds vcsStatus_ directly from pre-parsed entries against
    // the current editor::ProjectRoot(), bypassing Runner/a real
    // subprocess entirely so a test can assert on Paint()'s resulting row
    // colors without a live git repo.
    void DispatchVcsStatusForTesting(const std::vector<editor::vcs::StatusEntry>& entries);

    // sidebar-context-menu follow-up: a right-press on a tree row reports
    // that entry's path/isDirectory plus the click's absolute screen
    // position -- TabBar's own SetOnContextMenuRequest shape, since building
    // the actual popup needs main.cpp's OverlayHost/ListPopup and
    // WindowManager, neither of which this widget knows about. Unlike a left
    // click, this never toggles/opens the entry itself. Unset (the default)
    // means right-click is a no-op, matching every other Set* hook here.
    void SetOnContextMenuRequest(std::function<void(const std::filesystem::path&, bool isDirectory, Point anchor)> handler);

  private:
    std::function<ActiveBuffer&()>                                 activeBufferProvider_;
    text::BufferList&                                              bufferList_;
    std::string&                                                   statusMessage_;
    const Theme&                                                   theme_;
    std::function<void(text::Buffer&)>                             onBufferClosed_;
    std::function<void(const std::filesystem::path&)>              onBinaryFileOpenRequest_; // see SetOnBinaryFileOpenRequest()
    std::function<void(const std::filesystem::path&, bool, Point)> onContextMenuRequest_;    // see SetOnContextMenuRequest

    int scrollOffset_ = 0; // first visible row (post-sticky-headers), in *visible* (post-collapse) tree-entry units

    // Directories the user has expanded, by absolute path. A directory not
    // in this set (which is every directory, initially) renders collapsed:
    // listed itself, but its own children filtered out of the visible list.
    std::set<std::filesystem::path> expandedDirs_;

    // project-sidebar-drag-drop follow-up: see DraggingFilePath's own doc comment.
    std::optional<std::filesystem::path> dragPath_;

    // Double-click detection for file rows (single-click-preview follow-up):
    // a second click on the *same path* within kDoubleClickWindow counts as
    // a double click. Directory clicks (expand/collapse) don't go through
    // this at all -- only file opens care about single vs. double.
    std::optional<std::filesystem::path>  lastFileClickPath_;
    std::chrono::steady_clock::time_point lastFileClickTime_;

    void OpenFileEntry(const std::filesystem::path& path, bool isDoubleClick);

    // sidebar-keyboard-focus follow-up -- see Focusable() above.
    std::function<void()> onFocusReturn_;

    // named-projects follow-up -- see SetOnHeaderClicked above.
    std::function<void()> onHeaderClicked_;

    int selectedIndex_ = 0; // index into VisibleEntries, clamped at use

    void ReturnFocus(); // fires onFocusReturn_ -- LeftDock::NoteFocusReturned is chained on at the wiring site

    bool HandleKeyEvent(const Event& event);
    void ToggleDirectory(const std::filesystem::path& path); // shared by mouse click and Enter
    void EnsureSelectionVisible();

    [[nodiscard]] std::vector<editor::ProjectTreeEntry> VisibleEntries(
        const std::vector<editor::ProjectTreeEntry>& all) const;

    // sidebar-header follow-up: row 0 is always the project-name header,
    // never tree content (unified-left-dock follow-up: no separate bottom
    // border row to exclude anymore -- LeftDock's own interior box already
    // excludes its border, so every row this widget is handed is either the
    // header or real content). Every tree-row computation (ComputeRowLayout/
    // EntryIndexAtRow's viewportHeight, the wheel-scroll clamp) works in
    // this content-only height, not size().height directly, and every
    // row/y value crossing that boundary gets shifted by kHeaderHeight
    // exactly once, at the call site.
    [[nodiscard]] int ContentHeight() const;

    // BuildProjectTree does a full recursive directory walk -- cheap for a
    // small project, genuinely expensive (tens of milliseconds, measured)
    // for a large one, and this widget's Paint()/OnEvent() used to call it
    // unconditionally on every single call. Every widget repaints fresh
    // every frame, i.e. every keystroke, even ones with nothing to do with
    // the sidebar at all -- calling it unconditionally was a real, reported,
    // felt typing/cursor lag, not a hypothetical one. CachedTree() rebuilds
    // at most once per
    // kTreeCacheThrottle, or immediately if ProjectRoot() changed or
    // InvalidateTree() was called since the last build; every other frame
    // reuses the cached result. A bounded, deliberately simple fix (no file-
    // system watcher) -- external changes (e.g. `git checkout` in another
    // terminal) can lag up to kTreeCacheThrottle behind, an accepted
    // trade-off for a project-tree sidebar, not a correctness requirement.
    //
    // project-sidebar-eager-walk follow-up: the throttle alone still meant
    // a full, unconditional recursive walk of the *entire* tree every
    // kTreeCacheThrottle window, even through directories the user has
    // never expanded -- fine for a small git-rooted project, but a real,
    // reported, continuous lag for a file opened with no VCS marker above
    // it, where ProjectRoot() falls back to the file's whole containing
    // directory (see ProjectRoot.h's DetectProjectRoot) -- e.g. a file
    // opened directly under $HOME made this walk the user's entire home
    // directory twice a second. CachedTree() now passes BuildProjectTree an
    // expandedDirs_-checking predicate so a directory is still always
    // listed, but its children are only walked if the user has actually
    // expanded it.
    [[nodiscard]] const std::vector<editor::ProjectTreeEntry>& CachedTree();

    std::vector<editor::ProjectTreeEntry> treeCache_;
    bool                                  treeCacheValid_ = false;
    std::filesystem::path                 treeCacheRoot_;
    std::chrono::steady_clock::time_point treeCacheTime_;

    // changed-files-highlight follow-up: absolute path -> most severe status
    // at or below that path (a file's own status, or the max over a
    // directory's descendants -- see RefreshVcsStatus's own comment).
    // Rebuilt on the same kTreeCacheThrottle cadence CachedTree() already
    // uses (piggybacked onto that same "due for a refresh" check, not a
    // second timer) -- deliberately not more eager than that: like the tree
    // walk itself, a git-status lag of up to kTreeCacheThrottle is an
    // accepted trade-off here, not a correctness requirement.
    std::unordered_map<std::filesystem::path, RowStatus> vcsStatus_;
    editor::vcs::Runner*                                 vcsRunner_ = nullptr;
    void                                                    RefreshVcsStatus(const std::filesystem::path& root);
};

} // namespace ned::ui

#endif // NED_UI_PROJECTSIDEBAR_H
