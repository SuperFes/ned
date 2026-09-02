//
// VCS side panel (core slice -- see ROADMAP.md's "VCS Side Panel (New
// Feature)" entry): a persistent, ProjectSidebar-shaped tree of the working
// tree's staged/unstaged/untracked files, with multi-select batch stage/
// unstage and inline commit/branch triggers. Physically modeled directly on
// ProjectSidebar.h/.cpp -- same rounded-border/collapse-to-a-strip/drag-
// resize/keyboard-focus shape -- docked on the left, swappable with
// ProjectSidebar rather than shown alongside it (see
// BufferView::SetVcsPanel's own doc comment for how the swap is kept
// mutually exclusive when driven by the toggle-vcs-panel/toggle-project-
// sidebar commands; a manual divider double-click on either widget can
// still show both at once, an accepted v1 edge case).
//
// Each of the three sections (staged/unstaged/untracked -- VcsRowStatus's
// own vocabulary doesn't distinguish these, this panel adds that on top via
// Editor/Vcs/VcsRowStatus.h's PartitionVcsStatus) groups its files into a
// directory tree, not a flat list, mirroring ProjectSidebar's own
// BuildProjectTree-backed rendering -- built here from a known path list
// (VcsStatusEntry paths) rather than a disk walk, via VcsPanel.cpp's own
// BuildStatusTree. Row indentation only, no box-drawing tree connectors --
// a deliberate v1 simplification, see VcsPanel.cpp's own comment.
//

#ifndef NED_UI_VCSPANEL_H
#define NED_UI_VCSPANEL_H

#include <chrono>
#include <filesystem>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "ActiveBuffer.h"
#include "Editor/ProjectTree.h"
#include "Editor/Vcs/VcsRowStatus.h"
#include "Editor/Vcs/VcsRunner.h"
#include "Text/BufferList.h"
#include "Theme.h"
#include "Widget.h"

namespace ned::ui {

// What a panel-triggered action asks the focused pane's BufferView to do --
// see VcsPanel::SetOnAction's own doc comment. Commit/SwitchBranch/
// CreateBranch all reuse an existing BufferView interactive flow verbatim
// (BeginVcsCommitMessage/BeginVcsSwitchBranchPrompt/the vcs-create-branch
// prompt) -- this panel adds no new commit/branch primitive of its own,
// just a second entry point into flows that already work from C-c v c/w/n.
enum class VcsPanelAction { Commit, SwitchBranch, CreateBranch };

// Which working-tree/stash bucket a row belongs to. Stash support follow-up:
// Stash is a fourth bucket, distinct from the three working-tree ones --
// unlike them, its section is only ever shown when non-empty (see
// VcsPanel::BuildRows's own comment).
enum class VcsPanelSection { Staged, Unstaged, Untracked, Stash };

class VcsPanel : public Widget {
  public:
    // activeBufferProvider/bufferList/statusMessage/theme: same contract as
    // ProjectSidebar's own constructor (see its header comment) --
    // activeBufferProvider is re-resolved on every open so Enter/click
    // always opens into whichever pane currently has keyboard focus.
    VcsPanel(std::function<ActiveBuffer&()> activeBufferProvider, text::BufferList& bufferList,
             std::string& statusMessage, const Theme& theme);

    void Paint(Canvas c) override;
    bool OnEvent(const Event& event) override;

    [[nodiscard]] bool Focusable() const override {
        return true;
    }

    // See ProjectSidebar::SetOnFocusReturn's own doc comment -- identical
    // contract, wired to WindowManager::TakeFocus the same way.
    void SetOnFocusReturn(std::function<void()> handler);

    // focus-vcs-panel's entry point -- ProjectSidebar::TakeKeyboardFocus's
    // own contract (expands if collapsed, remembers to re-collapse on
    // focus-return if it was).
    void TakeKeyboardFocus();

    // Fired when the panel wants the focused pane's BufferView to start an
    // existing VCS interactive flow (commit compose / branch switch /
    // branch create) -- this widget has no BufferView& of its own, the same
    // reason ProjectSidebar routes its own binary-file-open/header-click
    // through callbacks rather than reaching for one directly.
    // WindowManager::RequestVcsPanelAction is main.cpp's wiring target.
    // Unset (the default) is a safe no-op, matching every other Set* hook
    // in this codebase.
    void SetOnAction(std::function<void(VcsPanelAction)> handler);

    // Inline diff preview: fired whenever the keyboard/mouse selection
    // moves onto (or off of) a staged/unstaged file row -- std::nullopt for
    // every other row kind (a section header, a directory, an untracked
    // file with no meaningful `git diff`, or a stash entry). staged mirrors
    // VcsRunner::RequestFileDiffText's own parameter (which section the row
    // is in). main.cpp wires this to request the file's diff text and feed
    // VcsDiffPreview, showing/hiding its overlay on Some/nullopt. Unset
    // (the default) is a safe no-op, matching every other Set* hook here.
    void SetOnSelectionChanged(std::function<void(std::optional<std::filesystem::path>, bool staged)> handler);

    // Inline diff preview: forces an immediate status refresh, the same
    // "force=true" path stage/unstage/commit/etc. already use internally --
    // public so main.cpp's own hunk-stage-toggle wiring (VcsDiffPreview::
    // SetOnHunkStageToggle) can bring this panel's section counts back in
    // sync right after a hunk apply, without waiting out the throttle.
    void ForceRefresh();

    [[nodiscard]] int  Width() const;
    void               SetWidth(int width);
    [[nodiscard]] bool Collapsed() const;
    void               SetCollapsed(bool collapsed);
    void               ToggleCollapsed();
    [[nodiscard]] int  ExpandedWidth() const;

    [[nodiscard]] bool IsResizing() const;
    void               UpdateResize(int globalMouseX);
    void               EndResize();

    // sidebar-width-memory follow-up's own precedent, this widget's
    // sibling: fires only for a divider drag that actually moved the width
    // (EndResize), or a deliberate collapse toggle (ToggleCollapsed) -- not
    // for a programmatic SetWidth/SetCollapsed call (session restore,
    // TakeKeyboardFocus's transient expand). Unset (the default) is a safe
    // no-op; main.cpp wires these to a "vcs-panel-width"/"vcs-panel-visible"
    // variable pair, ProjectSidebar's own "sidebar-width"/"sidebar-visible"
    // shape.
    void SetOnWidthCommitted(std::function<void(int)> handler);
    void SetOnCollapseCommitted(std::function<void(bool)> handler);

    // changed-files-highlight/VCS-side-panel: unset (the default) leaves
    // the panel showing "no VCS provider configured" -- main.cpp wires this
    // the same place it wires WindowManager::SetVcsRunner.
    void SetVcsRunner(editor::vcs::VcsRunner* vcsRunner);

    // Testing-only entry point, ProjectSidebar::DispatchVcsStatusForTesting's
    // own precedent -- builds the section trees directly from pre-parsed
    // entries, bypassing VcsRunner/a real subprocess entirely.
    void DispatchVcsStatusForTesting(const std::vector<editor::vcs::VcsStatusEntry>& entries);

    // Conflict-file affordance: DispatchVcsStatusForTesting bypasses
    // VcsRunner entirely (ProjectSidebar's own precedent), so it never
    // drives the real-disk-read conflict scan RefreshStatus's success
    // callback normally triggers -- this exposes that scan directly for a
    // test to call after seeding sections_ via DispatchVcsStatusForTesting.
    void RefreshConflictedPathsForTesting() {
        RefreshConflictedPaths();
    }

    // Stash support: same bypass-VcsRunner-entirely testing precedent as
    // DispatchVcsStatusForTesting above.
    void DispatchStashesForTesting(std::vector<editor::vcs::VcsStashEntry> entries) {
        stashes_ = std::move(entries);
    }

    // Test-only introspection: the currently marked (multi-selected) paths,
    // in no particular order -- same "small, honest introspection point"
    // reason ScrollArrowButton::IsRepeating() exists.
    [[nodiscard]] const std::set<std::filesystem::path>& SelectedPathsForTesting() const {
        return selected_;
    }

  private:
    std::function<ActiveBuffer&()> activeBufferProvider_;
    text::BufferList&              bufferList_;
    std::string&                   statusMessage_;
    const Theme&                   theme_;

    int  width_     = 30;
    bool collapsed_ = false;

    bool resizing_            = false;
    int  resizeAnchorGlobalX_ = 0;
    int  resizeAnchorWidth_   = 0;
    int  resizeStartWidth_    = 0;
    void BeginResize(int globalMouseX);

    std::function<void(int)>  onWidthCommitted_;
    std::function<void(bool)> onCollapseCommitted_;
    void                      CommitCollapsed(bool collapsed);

    int scrollOffset_  = 0;
    int selectedIndex_ = 0;

    // Directories the user has expanded, shared across all three sections
    // (a directory's expand state is one fact, regardless of which
    // section(s) currently show it) -- ProjectSidebar::expandedDirs_'s own
    // shape.
    std::set<std::filesystem::path> expandedDirs_;

    // Sections start expanded; a section present in this set renders just
    // its header row, contents collapsed -- same convention a directory
    // uses via expandedDirs_ above, inverted (present = collapsed here,
    // since every section starts open but a directory starts closed).
    std::set<VcsPanelSection> collapsedSections_;

    // Multi-select marks (dired-style), Space toggles the focused row.
    std::set<std::filesystem::path> selected_;

    std::function<void()> onFocusReturn_;
    bool                  collapseOnFocusReturn_ = false;
    void                  ReturnFocus();

    std::function<void(VcsPanelAction)> onAction_;

    // Inline diff preview -- see SetOnSelectionChanged's own doc comment.
    // lastNotified_ suppresses re-firing for the same (path, staged) pair
    // (e.g. Down past a stash section back onto the same file row is a
    // no-op re-request otherwise possible if BuildRows' row count changed
    // around it) -- nullopt means "last notification was nullopt too".
    std::function<void(std::optional<std::filesystem::path>, bool)> onSelectionChanged_;
    std::optional<std::pair<std::filesystem::path, bool>>           lastNotifiedSelection_;
    void                                                              NotifySelectionChanged();

    editor::vcs::VcsRunner*        vcsRunner_ = nullptr;
    editor::vcs::VcsStatusSections sections_;
    bool                           haveStatus_ = false;

    // Conflict-file affordance: absolute paths (of the staged/unstaged
    // entries currently in `sections_`) whose on-disk content contains real
    // <<<<<<< conflict markers (Text/ThreeWayMerge.h's HasConflictMarkers,
    // save-buffer's own guard precedent) -- recomputed on the same
    // throttled cadence as sections_ itself, not per-frame. Untracked files
    // are never checked -- "conflict" is a merge concept that doesn't apply
    // to a file git doesn't know about yet.
    std::set<std::filesystem::path> conflictedPaths_;
    void                            RefreshConflictedPaths();

    // Stash support: refreshed on the same throttled cadence as sections_.
    std::vector<editor::vcs::VcsStashEntry> stashes_;
    void                                    PushStash();
    void                                    PopStash(const std::string& ref);
    void                                    DropStash(const std::string& ref);

    // Branch switcher/creator inline: the checked-out branch, shown in the
    // border title (ProjectSidebar's own header-row precedent) rather than
    // a dedicated content row -- "VCS \xC2\xB7 <branch> \xC2\xB7 N staged"
    // -- so no extra row-layout plumbing is needed for what the ROADMAP
    // calls a header sub-row. Refreshed on the same throttled tick
    // RefreshStatus already runs.
    std::optional<std::string> currentBranch_;

    // Push/pull/fetch + ahead/behind summary: nullopt until the first
    // successful RequestAheadBehind (e.g. no upstream configured) -- the
    // title shows nothing extra in that case rather than "0 0", which
    // would misleadingly claim "up to date" when the fact is actually
    // unknown.
    std::optional<editor::vcs::VcsAheadBehind> aheadBehind_;

    enum class RemoteAction { Fetch, Pull, Push };
    void RunRemoteAction(RemoteAction action);

    // Own throttled poll, independent of ProjectSidebar's own cache timer
    // (VCS-side-panel follow-up doc comment, VcsPanel.h's own header) --
    // the two widgets show different projections of the same status, not a
    // shared cache.
    std::chrono::steady_clock::time_point lastRefreshTime_;
    void                                  RefreshStatus(bool force);

    // In-flight batch stage/unstage requests -- status is force-refreshed
    // once this drops back to 0.
    int  pendingBatchOps_ = 0;
    void StageOrUnstageSelectionOrFocused(bool stage);

    // Discard/revert: the one destructive action in this panel -- 'x' on a
    // file row enters this confirm state (rendered in place of the border
    // title, see Paint()) instead of acting immediately. This widget has no
    // BufferView/MinibufferPrompt to borrow a y/n prompt from, so it's a
    // small, self-contained piece of state rather than reusing a shared
    // mechanism that doesn't exist here.
    std::optional<std::filesystem::path> pendingRevertConfirm_;

    [[nodiscard]] int ContentHeight() const;

    struct Row {
        enum class Kind { SectionHeader, Entry, StashEntry };
        Kind                      kind;
        VcsPanelSection           section;
        std::size_t               fileCount = 0; // SectionHeader only
        editor::ProjectTreeEntry  entry{};        // Entry only
        editor::vcs::VcsRowStatus status = editor::vcs::VcsRowStatus::None; // Entry (file rows) only
        bool                      conflicted = false; // Entry (file rows) only -- see conflictedPaths_
        editor::vcs::VcsStashEntry stash{};      // StashEntry only
    };
    [[nodiscard]] std::vector<Row> BuildRows() const;

    void ToggleDirectory(const std::filesystem::path& path);
    void OpenFileEntry(const std::filesystem::path& path);
    bool HandleKeyEvent(const Event& event);
    void EnsureSelectionVisible();
};

} // namespace ned::ui

#endif // NED_UI_VCSPANEL_H
