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
// see ROADMAP.md.
//
// Chrome-redesign follow-up: the whole widget is framed by a rounded
// border (Border.h) with the project name embedded in the top edge as the
// header -- row 0 (the title row) and the bottom row are border, tree
// content lives in rows [1, height-2] and columns [1, width-2]. The right
// border column doubles as the resize divider (below) and its whole frame
// takes the accent brush while a drag is live. Hiding/showing is a
// *collapse* now, not Widget::active (which stays permanently true): while
// Collapsed(), Width() reports 1 and Paint() draws a single border-column
// strip with an accent hint glyph, so a mouse affordance to reopen never
// vanishes -- this is what replaced the separate SidebarToggle widget.
// Double-clicking the divider/strip toggles the collapse (same
// kDoubleClickWindow timer file rows already use); C-c C-p
// (toggle-project-sidebar) does the same from the keyboard via
// ToggleCollapsed().
//
// The width is drag-resizable by the divider column (round-2 follow-up):
// pressing on it starts a resize session (IsResizing()/UpdateResize()/
// EndResize()), which BufferView cooperates in -- there's no
// mouse-capture concept (every mouse event is delivered to every leaf
// widget regardless of position; see Widget.h's own header comment), so
// once a growing drag crosses out of this widget's own bounds the move
// events land on BufferView too. BufferView checks IsResizing() in its own
// OnEvent and, if set, drives the resize instead of its usual selection/
// no-op handling. See UpdateResize's own comment for why the math is
// anchored to the drag's start rather than applied as a per-event delta.
// Width() is read by main.cpp's own composition root every frame to decide
// this widget's actual layout width, since layout is recomputed fresh each
// frame rather than cached.
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
#include "Editor/ProjectTree.h"
#include "Editor/Vcs/VcsRunner.h"
#include "Text/BufferList.h"
#include "Theme.h"
#include "Widget.h"

namespace ned::ui {

// changed-files-highlight follow-up: how severe a row's (or, for a
// directory, its most severe descendant's) git status is, ordered
// least-to-most severe so merging two children's statuses is a plain
// std::max. Deliberately just these four buckets, not a verbatim porcelain
// code -- a tree row only needs to know which color to paint, the same
// "don't reinterpret VCS-specific text beyond what the UI needs" call
// BufferView's own DiffLineKind already makes for the per-line diff gutter.
enum class VcsRowStatus { None, Untracked, Added, Modified, Deleted };

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

    // focus-project-sidebar's (C-c p) entry point: expands a collapsed
    // sidebar first (focus into a 1-column strip would be meaningless) and
    // remembers that it *was* collapsed, so returning focus (Escape/C-g, or
    // Enter opening a file) collapses it again -- a hidden sidebar summoned
    // by keyboard goes back to hidden when the keyboard leaves, instead of
    // staying expanded as a side effect of a quick file jump.
    void TakeKeyboardFocus();

    // Called with the new width when a divider drag ends having actually
    // moved it (sidebar-width-memory follow-up). Unset (the default) is a
    // safe no-op, matching every other Set* hook here; main.cpp wires this
    // to editor::SetVariable("sidebar-width") so the last committed width
    // becomes the global default for future runs -- the persistence policy
    // deliberately lives at the wiring site, not in this widget, so
    // unit-test drags never touch the real variables.json.
    void SetOnWidthCommitted(std::function<void(int)> handler);

    // Called with the new collapse state when the *user* deliberately
    // toggles it (toggle-project-sidebar, or a divider/strip double-click)
    // -- never for programmatic changes: session restore, and
    // TakeKeyboardFocus's transient expand plus its focus-return restore,
    // go through SetCollapsed directly and stay uncommitted, so a quick
    // C-c p file jump can't overwrite the remembered preference. Unset is
    // a safe no-op; main.cpp wires this to
    // editor::SetVariable("sidebar-visible"), SetOnWidthCommitted's exact
    // pattern.
    void SetOnCollapseCommitted(std::function<void(bool)> handler);

    // Current desired width in columns -- see this file's own header
    // comment for why main.cpp's composition root reads this every frame
    // rather than this widget mutating a stored layout policy directly.
    // Starts at initialWidth (main.cpp passes the same 30 the pre-migration
    // version's initial fixed(30) used); UpdateResize below and SetWidth
    // (session-persistence slice 2: main.cpp applying a restored session's
    // sidebar width at startup) are the only things that ever change it
    // afterward.
    [[nodiscard]] int Width() const;
    void              SetWidth(int width); // clamped to kMinSidebarWidth, same as a resize drag

    // Collapse state (chrome-redesign follow-up) -- see this file's own
    // header comment. Width() reports 1 while collapsed; width_ itself is
    // untouched, so expanding restores the previous width exactly.
    // Session persistence maps its stored sidebar-visibility bool onto
    // !Collapsed() (main.cpp / WindowManager), no schema change needed.
    [[nodiscard]] bool Collapsed() const;
    void               SetCollapsed(bool collapsed);
    void               ToggleCollapsed();

    // The width an expanded sidebar has/would have -- unlike Width(), not
    // masked by the collapse. What session persistence stores, so a resize
    // survives a collapsed quit/relaunch.
    [[nodiscard]] int ExpandedWidth() const;

    [[nodiscard]] bool IsResizing() const;

    // Called by BufferView's own OnEvent while IsResizing() -- see this
    // header's comment above for why BufferView needs to be involved at
    // all. globalMouseX is the raw, absolute (screen-space) mouse x -- no
    // translation from a caller-local coordinate is needed, since mouse
    // coordinates are never translated to begin with (see Widget.h's own
    // header comment), so this is just whichever widget's OnEvent received
    // the event passing its raw event.mouse().x straight through.
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
    // (VcsRunner::RequestStatus's onError just fires, vcsStatus_ stays
    // empty). main.cpp wires this the same place it wires
    // WindowManager::SetVcsRunner.
    void SetVcsRunner(editor::vcs::VcsRunner* vcsRunner);

    // Testing-only entry point, BufferView::DispatchStatusForTesting's own
    // precedent: builds vcsStatus_ directly from pre-parsed entries against
    // the current editor::ProjectRoot(), bypassing VcsRunner/a real
    // subprocess entirely so a test can assert on Paint()'s resulting row
    // colors without a live git repo.
    void DispatchVcsStatusForTesting(const std::vector<editor::vcs::VcsStatusEntry>& entries);

  private:
    std::function<ActiveBuffer&()>                    activeBufferProvider_;
    text::BufferList&                                 bufferList_;
    std::string&                                      statusMessage_;
    const Theme&                                      theme_;
    std::function<void(text::Buffer&)>                onBufferClosed_;
    std::function<void(const std::filesystem::path&)> onBinaryFileOpenRequest_; // see SetOnBinaryFileOpenRequest()

    int scrollOffset_ = 0; // first visible row (post-sticky-headers), in *visible* (post-collapse) tree-entry units

    // Directories the user has expanded, by absolute path. A directory not
    // in this set (which is every directory, initially) renders collapsed:
    // listed itself, but its own children filtered out of the visible list.
    std::set<std::filesystem::path> expandedDirs_;

    int width_ = 30; // see Width()

    bool collapsed_ = false; // see Collapsed()

    bool resizing_            = false;
    int  resizeAnchorGlobalX_ = 0; // this Row-space mouse x when the drag started
    int  resizeAnchorWidth_   = 0; // this widget's own width when the drag started
    int  resizeStartWidth_    = 0; // width_ at BeginResize -- EndResize persists to
                                   // variables.json only if the drag actually changed it

    // Double-click detection for the divider/collapsed strip (chrome-
    // redesign follow-up): a second press within kDoubleClickWindow toggles
    // the collapse; a real drag (movement past +-1 column, see
    // UpdateResize) clears the pending state so drag-resize never
    // accidentally collapses.
    bool                                  dividerClickPending_ = false;
    std::chrono::steady_clock::time_point lastDividerPressTime_;

    void BeginResize(int globalMouseX);

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

    std::function<void(int)> onWidthCommitted_; // see SetOnWidthCommitted

    std::function<void(bool)> onCollapseCommitted_;            // see SetOnCollapseCommitted
    void                      CommitCollapsed(bool collapsed); // SetCollapsed + the deliberate-toggle hook
    int                   selectedIndex_ = 0; // index into VisibleEntries, clamped at use

    // See TakeKeyboardFocus. Cleared by ReturnFocus (the restore is
    // one-shot) and by an explicit collapse while focused, whose end state
    // is already what the flag would restore.
    bool collapseOnFocusReturn_ = false;
    void ReturnFocus(); // onFocusReturn_ plus the TakeKeyboardFocus collapse restore

    bool HandleKeyEvent(const Event& event);
    void ToggleDirectory(const std::filesystem::path& path); // shared by mouse click and Enter
    void EnsureSelectionVisible();

    [[nodiscard]] std::vector<editor::ProjectTreeEntry> VisibleEntries(
        const std::vector<editor::ProjectTreeEntry>& all) const;

    // sidebar-header follow-up: row 0 is always the project-name header
    // (the title-carrying top border since the chrome redesign), and the
    // bottom row is border too -- never tree content. Every tree-row
    // computation (ComputeRowLayout/EntryIndexAtRow's viewportHeight, the
    // wheel-scroll clamp) works in this content-only height, not
    // size().height directly, and every row/y value crossing that boundary
    // gets shifted by kHeaderHeight exactly once, at the call site.
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
    std::unordered_map<std::filesystem::path, VcsRowStatus> vcsStatus_;
    editor::vcs::VcsRunner*                                 vcsRunner_ = nullptr;
    void RefreshVcsStatus(const std::filesystem::path& root);
};

} // namespace ned::ui

#endif // NED_UI_PROJECTSIDEBAR_H
