//
// Renders a Buffer's visible lines and forwards key input to a Dispatcher.
// The core "actual editor" widget.
//
// Also drives interactive sub-sessions (isearch, query-replace, quit
// confirmation, find-file, switch-to-buffer) directly: while one is active,
// key events route to it instead of Dispatcher. There is no separate
// minibuffer widget for this -- live status text is written into the same
// shared status-message string EchoArea already displays.
//

#ifndef NED_UI_BUFFERVIEW_H
#define NED_UI_BUFFERVIEW_H

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ActiveBuffer.h"
#include "Editor/Acp/AcpManager.h"
#include "Editor/Backup.h"
#include "Editor/CodeFold.h"
#include "Editor/Command.h"
#include "Editor/Dap/DapManager.h"
#include "Editor/DiffRefreshSettings.h"
#include "Editor/Dispatcher.h"
#include "Editor/IncrementalSearch.h"
#include "Editor/Link.h"
#include "Editor/Lsp/LspManager.h"
#include "Editor/MinibufferPrompt.h"
#include "Editor/Mode.h"
#include "Editor/Org.h"
#include "Editor/PrefixArgument.h"
#include "Editor/ProjectReplace.h"
#include "Editor/ProjectTrust.h"
#include "Editor/PromptHistory.h"
#include "Editor/QueryReplace.h"
#include "Editor/Register.h"
#include "Editor/Tasks/TaskRunner.h"
#include "Editor/Vcs/VcsProvider.h"
#include "Editor/Vcs/VcsRunner.h"
#include "EventLoop.h"
#include "Minimap.h"
#include "ProjectSidebar.h"
#include "ScrollArrowButton.h"
#include "ScrollBar.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "Text/KillRing.h"
#include "Theme.h"

namespace ned::ui {

class BufferView : public Widget {
  public:
    // statusMessage is where a caught command exception, a command like
    // save-buffer reporting its own outcome, or live isearch/query-replace/
    // prompt status text gets written -- see EchoArea, which displays
    // whatever this currently holds. activeBuffer, mode, and theme must
    // outlive this BufferView (matches how killRing/bufferList/dispatcher/
    // statusMessage are already externally-owned references with the same
    // requirement). promptHistory is shared app-wide (like killRing/registers,
    // not per-pane) -- minibuffer-history-recall follow-up, see
    // TryNavigatePromptHistory's own doc comment.
    BufferView(ActiveBuffer& activeBuffer, text::KillRing& killRing, editor::RegisterTable& registers,
               editor::PromptHistory& promptHistory, text::BufferList& bufferList, editor::Dispatcher& dispatcher,
               std::string& statusMessage, const editor::Mode& mode, const Theme& theme);

    BufferView(const BufferView&)            = delete;
    BufferView& operator=(const BufferView&) = delete;

    void Paint(Canvas c) override;
    bool OnEvent(const Event& event) override;
    bool Focusable() const override; // was FocusPolicy::Strong

    // FTXUI -> Notcurses migration: there is no more per-frame OnAnimation
    // hook to override here (Notcurses' own EventLoop has no free-running
    // render-loop tick at all) -- the automatic-completion debounce and
    // status-message idle-timeout deadlines this used to drive both fire
    // via DeadlineTimer (EventLoop.h) instead now; see
    // completionDebounceDeadline_/statusMessageChangedAt_'s own doc
    // comments.

    // Local cursor position for the real terminal caret -- was ox::Widget's
    // own `cursor` field. A pure, independent computation, deliberately NOT
    // cached from Paint() -- see the .cpp definition's own comment for why
    // that caused a real, reported one-frame-stale cursor bug.
    [[nodiscard]] std::optional<Point> CursorPosition() const override;

    // Scroll-bar follow-up: topLine_ read/write for an externally-owned
    // ScrollBar to sync against. SetTopLine clamps the same way wheel
    // scrolling already does. SetScrollBar registers the bar Paint() keeps
    // in sync each frame (scrollable_length/position/item_visual_length) --
    // nullptr (the default) means no scroll bar is wired in, a no-op in
    // Paint(). The reverse direction (bar drag/wheel -> BufferView) is wired
    // by the caller connecting ScrollBar::SetOnScroll to SetTopLine
    // directly.
    [[nodiscard]] std::size_t TopLine() const;
    void                      SetTopLine(std::size_t line);
    void                      SetScrollBar(ScrollBar* scrollBar);

    // Minimap widget follow-up: registers the Minimap this pane's
    // ScrollBar column has been replaced by (WindowManager keeps
    // exactly one of the two active at a time) -- synced the same 3
    // fields SetScrollBar's own target gets, every Paint() call. nullptr
    // (the default) is a safe no-op, same "unset is a safe no-op"
    // convention SetScrollBar/SetProjectSidebar already establish.
    // scrollColumn is the sibling Widget (WindowManager::Pane's own
    // scrollColumn_ container) whose Widget::active flag toggle-minimap
    // must flip in lockstep opposition to minimap's -- BufferView has no
    // other way to reach a Pane-level sibling it doesn't own, so it's
    // handed the raw base-class pointer the same way projectSidebar_ is
    // handed a raw ProjectSidebar*. Initial opposite-ness is the caller's
    // responsibility (Pane seeds both from editor::MinimapEnabled() at
    // construction); this only ever flips both, never forces a state.
    void SetMinimap(Minimap* minimap, Widget* scrollColumn);

    // line-wrap follow-up: horizontal counterpart to TopLine()/SetTopLine(),
    // only ever meaningful for a buffer whose EffectiveWrapLines() is false
    // -- a wrapped line, by construction, never exceeds the viewport width,
    // so there is nothing left to horizontally scroll (see
    // ScrollToShowPointHorizontally's own doc comment). Public for the same
    // "externally observable scroll position" reason TopLine() is.
    [[nodiscard]] std::size_t LeftColumn() const;
    void                      SetLeftColumn(std::size_t column);

    // Registers the up/down arrow caps flanking the scroll bar so Paint()
    // can keep their enabled state in sync each frame: up is enabled only
    // when topLine_ > 0, down only when topLine_ < MaxTopLine() -- both
    // false at once when the whole buffer already fits on screen. Either or
    // both may be nullptr (the default) to opt out.
    void SetScrollArrows(ScrollArrowButton* up, ScrollArrowButton* down);

    // Registers the left-side project tree so toggle-project-sidebar
    // (project-sidebar follow-up) can flip its Widget::active flag; nullptr
    // (the default) means the toggle command is a no-op. Unlike the
    // pre-migration version, flipping .active alone is now sufficient --
    // no SetSidebarRow/forced-reflow equivalent is needed (FTXUI rebuilds
    // its element tree fresh every frame; confirmed during the TermOx ->
    // FTXUI migration, see ROADMAP.md).
    void SetProjectSidebar(ProjectSidebar* sidebar);

    // rich-theme-set follow-up (Phase 1): registers the callback the
    // select-theme picker applies a Theme through -- wired by main.cpp (via
    // WindowManager::SetThemeApplier) to an in-place assignment of the one
    // Theme local every widget holds `const Theme&` into, the exact swap
    // mechanism the ANSI fallback established there (and the applier is
    // also where the limited-terminal AnsiFallbackFor gate stays in the
    // loop -- this widget never needs to know about it). Unset (every
    // test-constructed BufferView that doesn't wire one) makes select-theme
    // report via statusMessage_ instead of opening a session -- the same
    // "unset is a safe no-op" convention SetProjectSidebar/SetLspManager
    // establish.
    void SetThemeApplier(std::function<void(const Theme&)> applier);

    // LSP client follow-up: registers the shared LspManager so Paint() can
    // call SyncBuffer for the active buffer every frame -- nullptr (the
    // default) means no-op, the same "unset is a safe no-op" convention
    // SetProjectSidebar/SetScrollBar already establish. Also every test-
    // constructed BufferView's own default: no test wires one in unless it
    // specifically wants to exercise LSP sync, so ordinary tests never touch
    // Lsp/ at all.
    void SetLspManager(editor::lsp::LspManager* lspManager);

    // task-runner follow-up: registers the shared TaskRunner, forwarded to
    // CommandContext::taskRunner before each dispatch so run-task/
    // cancel-task can reach it -- same "unset is a safe no-op" convention
    // SetLspManager already establishes.
    void SetTaskRunner(editor::tasks::TaskRunner* taskRunner);

    // VCS blame gutter: registers the shared VcsRunner -- same "unset is a
    // safe no-op" convention SetLspManager/SetTaskRunner already establish
    // (vcs-show-blame simply reports "no vcs runner configured" via
    // statusMessage_ if this was never called, the same way run-task
    // degrades with no TaskRunner).
    void SetVcsRunner(editor::vcs::VcsRunner* vcsRunner);

    // vcs-diff-gutter-staleness follow-up: a public entry point for
    // WindowManager to force this pane's diff gutter fresh from outside --
    // the periodic autosave-timer sweep (AutoRevertBuffers/AutoMergeBuffers'
    // own tick) and toggle-terminal's closing edge (running `git commit`/
    // `git checkout` in the embedded terminal, then closing it, previously
    // left every open buffer's gutter showing the pre-commit diff
    // indefinitely -- nothing in this codebase polls for VCS state changing
    // for a reason ned itself didn't cause). Just forwards to
    // RequestDiffForCurrentBuffer below, which already silently no-ops with
    // no VcsRunner wired -- this exists as its own public method rather than
    // widening that one's access, since its own doc comment specifically
    // describes two different, narrower existing call sites.
    void RefreshVcsDiff();

    // DAP client slice 1: registers the shared DapManager -- same "unset is
    // a safe no-op" convention as SetLspManager/SetTaskRunner/SetVcsRunner
    // (the dap-* commands report "No debugger available." via
    // statusMessage_ if this was never called).
    void SetDapManager(editor::dap::DapManager* dapManager);

    // ACP client slice 2: registers the shared AcpManager -- same "unset is
    // a safe no-op" convention as SetDapManager (the acp-* commands report
    // "No ACP manager available." via statusMessage_ if this was never
    // called).
    void SetAcpManager(editor::acp::AcpManager* acpManager);

    // ACP client slice 2: entered directly by WindowManager's AcpManager
    // wiring the moment a session/request_permission request arrives (an
    // agent-initiated request, never reached through the ordinary
    // InteractiveRequest/StartInteractiveSession path -- see
    // InteractiveRequest's own doc comment in Command.h) -- same "public so
    // an external async callback can drive this pane" shape JumpToPathLine
    // establishes for DapManager's stopped event. Enters
    // InputMode::AcpPermissionPrompt and renders prompt as a numbered
    // choice list (RefreshAcpPermissionPromptStatus), the same
    // LspCodeActionSelect shape.
    void ShowAcpPermissionPrompt(const editor::acp::AcpManager::PermissionPrompt& prompt);

    // Opens path (via BufferList::OpenOrCreateFile) and moves point to the
    // start of line (1-indexed, matching the "path:line" convention every
    // results-buffer format here writes). Reports any failure via
    // statusMessage_ rather than throwing -- callers never need their own
    // try/catch. Shared by VisitSearchResult/VisitVcsResult internally;
    // public (DAP client slice 1) so WindowManager's stopped-event callback
    // can jump the focused pane to wherever the debuggee stopped.
    void JumpToPathLine(const std::filesystem::path& path, std::size_t line);

    // vcs-show-blame's entry point (see StartInteractiveSession's
    // VcsShowBlame case) -- kicks off an async VcsRunner::RequestBlame for
    // the active buffer; on completion (which lands back here via
    // EventLoop::Post, same as every other async completion in this class)
    // populates blameLineInfo_ and the two generation-tracking fields below,
    // so the gutter starts rendering on the next Paint(). A no-op (reports
    // via statusMessage_) if no VcsRunner is registered.
    void RequestBlameForCurrentBuffer();

    // depth-aware-fold-gutter-style "only reserve the column when there's
    // something to show" gate -- true only once blameLineInfo_ has actually
    // been populated for the active buffer (never auto-triggered by
    // Paint() itself; see RequestBlameForCurrentBuffer's own doc comment
    // for why this can't be a per-Paint()-recomputed cache the way the
    // diagnostic/fold gutters are: populating it means running a real
    // subprocess, not a cheap synchronous scan).
    [[nodiscard]] bool BlameGutterActive() const;

    // Public primarily for tests -- mirrors TaskProcess::DispatchOutput/
    // DispatchExit's own "public primarily for tests" precedent (see that
    // class's doc comment): the real async path always reaches this via
    // VcsRunner::RequestBlame's onComplete callback (see
    // RequestBlameForCurrentBuffer), which requires a live, running
    // EventLoop to ever actually fire -- this codebase's established
    // convention is to never run one in a unit test (see TaskProcessTest.cpp/
    // TaskRunnerTest.cpp's own header comments). Calling this directly
    // exercises the exact same blameLineInfo_ population/cache-generation
    // update without needing one.
    void DispatchBlameForTesting(std::vector<editor::vcs::VcsBlameLine> lines);

    // Public primarily for tests -- same "public primarily for tests"
    // precedent DispatchBlameForTesting just above establishes, for the
    // same reason: the real async path always reaches this via
    // VcsRunner::RequestDiff's onComplete callback (see
    // RequestDiffForCurrentBuffer), which needs a live EventLoop to ever
    // fire. Converts raw hunks into diffLineKinds_ directly, exercising
    // the exact same classification RequestDiffForCurrentBuffer's own
    // completion handler uses.
    void DispatchDiffForTesting(std::vector<editor::vcs::VcsDiffHunk> hunks);

    // Public primarily for tests, same precedent again (vocabulary-
    // completion follow-up): the real async paths reach
    // BuildVcsStatusBuffer/BuildVcsBranchesBuffer/ResolveVcsFileTarget
    // via VcsRunner callbacks that need a live EventLoop to ever fire.
    // DispatchStatusForTesting is exactly RequestVcsStatusBuffer's
    // onComplete body; DispatchBranchesForTesting is
    // RequestVcsBranchesBuffer's; ResolveVcsFileTargetForTesting exposes
    // the status-line-at-point/active-buffer-path target resolution
    // stage/unstage share.
    void                                               DispatchStatusForTesting(std::vector<editor::vcs::VcsStatusEntry> entries);
    void                                               DispatchBranchesForTesting(std::vector<editor::vcs::VcsBranchEntry> entries);
    [[nodiscard]] std::optional<std::filesystem::path> ResolveVcsFileTargetForTesting();
    // Hunk-staging follow-up: same seam again, for StageOrUnstageHunkAtPoint's
    // synchronous guards (no runner / modified buffer / no path) -- the
    // async tail past them needs a live EventLoop.
    void StageHunkAtPointForTesting(bool stage);
    // multi-line-commit-message follow-up: same seam shape, for
    // BeginVcsCommitMessage/FinishVcsCommitMessage/AbortVcsCommitMessage --
    // RequestCommit's own guards (no provider registered) resolve
    // synchronously (see VcsRunnerTest.cpp), so FinishVcsCommitMessageForTesting
    // is fully exercisable without a live EventLoop too.
    void BeginVcsCommitMessageForTesting();
    void FinishVcsCommitMessageForTesting();
    void AbortVcsCommitMessageForTesting();

    // Diagnostics-multibuffer follow-up: same "public primarily for tests"
    // seam, but RequestDiagnosticsBuffer needs no live EventLoop at all --
    // it's fully synchronous, no VcsRunner-style callback in between. This
    // is a plain passthrough rather than a partial "guards only" exposure.
    void RequestDiagnosticsBufferForTesting();

    // find-all-references follow-up: same seam again, for
    // RequestProjectFindReferences -- fully synchronous, no live EventLoop
    // needed, same reasoning as RequestDiagnosticsBufferForTesting above.
    void RequestProjectFindReferencesForTesting();

    // FTXUI -> Notcurses migration: replaces
    // ftxui::ScreenInteractive::Active() (used to end the whole app on
    // `quit`/confirmed ConfirmQuit) and backs completionDebounceDeadline_/
    // statusMessageChangedAt_'s own DeadlineTimer-based deadlines (see their
    // doc comments below). Unset (the default, nullptr) makes `quit` a
    // no-op instead of a null-deref -- every unit test, and any other
    // headless use of BufferView, matching the exact null-check contract
    // ftxui::ScreenInteractive::Active() itself used to require here.
    void SetEventLoop(EventLoop* eventLoop);

    // Entry point for TabBar's close-icon click (tab-close follow-up) --
    // TabBar only ever signals *intent*, the same "mouse-driven widget hands
    // off to BufferView" shape SetProjectSidebar's callers already
    // establish, since only BufferView can drive a keyboard y/n
    // confirmation (TabBar takes no keyboard focus). An unmodified buffer
    // closes immediately; a modified one starts a ConfirmCloseBuffer
    // prompt, mirroring ConfirmQuit but scoped to this one buffer rather
    // than every buffer in the list. Closing the last remaining buffer
    // conjures a fresh scratch buffer as its replacement rather than
    // refusing -- BufferList must always have at least one buffer, and
    // there's nothing meaningful to show otherwise, the same call Emacs
    // itself makes for *scratch*. A no-op (reports via statusMessage_
    // instead of silently doing nothing) if another interactive session is
    // already in progress.
    void RequestCloseBuffer(text::Buffer& buffer);

    // open-binary-anyway follow-up: entry point for ProjectSidebar's click
    // handler (mouse-only, same "hands off to BufferView for anything
    // needing keyboard y/n" shape RequestCloseBuffer's own doc comment
    // describes) -- offers the same ConfirmOpenBinary y/n confirmation
    // find-file's own HandlePromptKey already gives a typed path, instead
    // of the sidebar just reporting BinaryFileError's refusal message and
    // stopping there. A no-op (reports via statusMessage_ instead of
    // silently doing nothing) if another interactive session is already in
    // progress -- same guard RequestCloseBuffer uses.
    void RequestOpenBinaryFile(const std::filesystem::path& path);

    // session-persistence slice 3: asks the user whether to load a
    // project's own .ned/init.janet -- a y/n/a prompt in the
    // RequestOpenBinaryFile mold (deferred from startup to here because
    // there's no way to ask anything before the UI exists). onDecision is
    // invoked exactly once with the user's choice, after the prompt session
    // has ended -- actually loading Janet is the caller's business
    // (main.cpp owns the janet::Environment), not this widget's. A no-op
    // (reports via statusMessage_, decision never delivered) if another
    // interactive session is already in progress, same guard as the other
    // Request* entry points -- acceptable for its one real call site,
    // startup, where no other session can exist yet.
    void RequestTrustProjectInit(const std::filesystem::path&                                                   initPath,
                                 std::function<void(const std::filesystem::path&, editor::ProjectInitDecision)> onDecision);

    // Window-splitting follow-up: called with the same InteractiveRequest
    // whenever StartInteractiveSession sees one of the five structural
    // window-management values (SplitBelow/SplitRight/DeleteWindow/
    // DeleteOtherWindows/OtherWindow) -- these operate above the level of a
    // single BufferView, so unlike every other InteractiveRequest this
    // class doesn't act on them itself, it just forwards. Mirrors
    // SetProjectSidebar/SetOnCloseRequest's own "connect after construction,
    // unset is a safe no-op" convention exactly. WindowManager (the owner of
    // however many BufferViews exist) is the intended registrant.
    void SetOnWindowRequest(std::function<void(editor::InteractiveRequest)> handler);

    // Window-splitting follow-up: called from CloseBufferNow, before the
    // buffer is actually erased from bufferList_, with the buffer that's
    // about to close -- so a multi-pane owner can retarget any *other* pane
    // whose ActiveBuffer also pointed at it (this BufferView's own
    // activeBuffer_ is already handled internally by CloseBufferNow). Unset
    // is a safe no-op, matching every other Set* hook here.
    void SetOnBufferClosed(std::function<void(text::Buffer&)> handler);

    // terminal-panel follow-up: toggle-terminal's forwarding hook -- the
    // panel is an OverlayHost overlay owned by main.cpp's composition, above
    // even WindowManager's level, so like the window-management requests
    // this class only forwards. main.cpp's three-state toggle lambda is the
    // intended registrant (via WindowManager::SetOnTerminalToggle, which
    // fans it out to every pane). Unset is a safe no-op.
    void SetOnTerminalToggle(std::function<void()> handler);

    // ACP chat panel: acp-toggle-panel's forwarding hook, same shape and
    // reasoning as SetOnTerminalToggle immediately above -- the panel is
    // another OverlayHost overlay owned by main.cpp's composition, wired via
    // WindowManager::SetOnAcpPanelToggle fanning out to every pane. Unset is
    // a safe no-op.
    void SetOnAcpPanelToggle(std::function<void()> handler);

    // per-buffer-mode follow-up: called at the top of Paint() whenever the
    // active buffer's identity has changed since the last Paint() call --
    // the same "recompute, don't cache, detect via pointer identity" idiom
    // topLineValidatedBuffer_/highlightCacheBuffer_/etc. already use, kept
    // as its own independent check since it serves an unrelated concern.
    // Does NOT fire on the very first Paint() after construction (see
    // modeSyncBuffer_'s own doc comment). Intended registrant is this
    // BufferView's owning Pane (WindowManager.cpp), which reassigns its own
    // Mode in place -- see Pane::mode_'s doc comment for why that alone is
    // sufficient to swap highlighting/folding/keymap/expand-selection with
    // no Dispatcher/KeymapStack rebuild needed. Unset is a safe no-op,
    // matching every other Set* hook here.
    void SetOnActiveBufferChanged(std::function<void(text::Buffer&)> handler);

    // per-buffer-highlight-cache follow-up: erases buffer's entry from this
    // pane's own highlightCacheByBuffer_/foldableBlocksCacheByBuffer_ (see
    // their own doc comments). Called from WindowManager::
    // ReassignPanesShowing -- the shared close funnel every real buffer
    // close already goes through -- for every pane, not just one currently
    // showing buffer, since a pane can hold a stale entry for a buffer it
    // merely visited in the past.
    void ClearBufferCaches(text::Buffer& buffer);

  private:
    enum class InputMode { Normal,
                           IsearchForward,
                           IsearchBackward,
                           QueryReplace,
                           ConfirmQuit,
                           FindFile,
                           SwitchToBuffer,
                           ProjectSearch,
                           ProjectReplace,
                           ConfirmCloseBuffer,
                           CreateDirectory,
                           DeleteFile,
                           RenameFile,
                           FindScratch,
                           // backup-and-recovery follow-up: recover-file's
                           // pick-a-version prompt -- dedicated handler
                           // (HandleRecoverFileKey), not a HandlePromptKey
                           // branch, for the same else-chain reason
                           // DeleteFile/RenameFile got their own.
                           RecoverFile,
                           // Emacs-coverage follow-up: goto-line's own prompt
                           // (M-g g) -- same single-line session shape as
                           // CreateDirectory, no completion.
                           GotoLine,
                           // external-modification-safety follow-up: save-buffer
                           // found the file changed on disk underneath the
                           // buffer -- y/n before overwriting, mirroring
                           // ConfirmCloseBuffer's shape.
                           ConfirmOverwriteSave,
                           // external-modification-round-2 follow-up: save-buffer
                           // found unresolved "<<<<<<<" conflict markers still in
                           // the buffer -- y/n before writing them to disk, same
                           // shape as ConfirmOverwriteSave.
                           ConfirmSaveWithConflicts,
                           ExecuteCommand,
                           ProjectFindFile,
                           PointToRegister,
                           JumpToRegister,
                           CopyToRegister,
                           InsertRegister,
                           // Emacs-keymap-round-2 follow-up: zap-to-char's
                           // own one-character-read session, same shape as
                           // the register modes above -- see
                           // HandleZapToCharKey.
                           ZapToChar,
                           StringRectangle,
                           SetHeadlineTags,
                           // code-actions follow-up: entered only once RequestCodeActionsAtPoint's
                           // async response actually arrives (never eagerly, while the request is
                           // still in flight -- see that method's own doc comment) -- Select when
                           // more than one action came back, Confirm for the (possibly only, or
                           // just-picked-from-Select) one awaiting a y/n.
                           LspCodeActionSelect,
                           LspCodeActionConfirm,
                           // go-to-definition follow-up: same "entered only from inside the
                           // async response callback" shape as LspCodeActionSelect above --
                           // Select when RequestDefinitionAtPoint's response names more than one
                           // location (a real, if less common, case -- e.g. a virtual/overridden
                           // method with several implementations).
                           LspGotoDefinitionSelect,
                           // rename follow-up: LspRenameNewName is the one synchronous
                           // prompt-shaped stage here (routed through HandlePromptKey, like
                           // FindFile/CreateDirectory/etc.) -- Enter sends the actual
                           // textDocument/rename request via RequestRenameAtPoint, which only
                           // then (from inside its own async callback, once the response
                           // arrives) enters LspRenameConfirm for the final y/n.
                           LspRenameNewName,
                           LspRenameConfirm,
                           // open-binary-anyway follow-up: entered only from
                           // inside HandlePromptKey's FindFile branch, when
                           // BufferList::OpenOrCreateFile throws
                           // text::BinaryFileError -- see
                           // pendingBinaryOpenPath_'s own doc comment.
                           ConfirmOpenBinary,
                           // session-persistence slice 3: the y/n/a trust
                           // prompt for a project's .ned/init.janet -- see
                           // RequestTrustProjectInit.
                           ConfirmTrustProjectInit,
                           // task-runner follow-up: one synchronous "task name"
                           // prompt, routed through HandlePromptKey like
                           // FindFile/CreateDirectory/LspRenameNewName above --
                           // shared by both run-task and cancel-task, distinguished
                           // by taskPromptAction_ (set alongside inputMode_ in
                           // StartInteractiveSession).
                           TaskName,
                           // DAP client slice 3: the evaluate prompt (dap-evaluate) --
                           // routed through HandlePromptKey like FindFile/TaskName; Enter
                           // fires the async DAP evaluate request, the result landing in
                           // statusMessage_ from its callback.
                           DapEvaluate,
                           // VCS vocabulary-completion follow-up: two more
                           // HandlePromptKey-routed prompts (the commit message itself
                           // no longer goes through MinibufferPrompt -- see
                           // InteractiveRequest::VcsCommit's own doc comment in
                           // Command.h for the real buffer it collects into instead).
                           // VcsSwitchBranch is entered from BeginVcsSwitchBranchPrompt's
                           // async branch-list callback (the RequestRenameAtPoint
                           // enter-a-mode-from-a-callback pattern) so Tab completes
                           // against vcsBranchCandidates_; VcsCreateBranch is a plain
                           // name prompt. Enter fires the matching async VcsRunner
                           // request fire-and-forget, results landing in statusMessage_
                           // from the callback (DapEvaluate's shape).
                           VcsSwitchBranch,
                           VcsCreateBranch,
                           // ACP client slice 2: AcpAgentName/AcpPromptText are
                           // HandlePromptKey-routed plain-text prompts (agent name,
                           // then message text), same shape as TaskName/DapEvaluate
                           // above. AcpPermissionPrompt is different in kind -- never
                           // entered through StartInteractiveSession/HandlePromptKey at
                           // all, since a session/request_permission request is
                           // agent-initiated, not user-command-initiated; entered
                           // directly by ShowAcpPermissionPrompt (called from
                           // WindowManager's AcpManager wiring) and driven by its own
                           // HandleAcpPermissionPromptKey, the same numbered-list shape
                           // LspCodeActionSelect uses.
                           AcpAgentName,
                           AcpPromptText,
                           AcpPermissionPrompt,
                           // rich-theme-set follow-up (Phase 1): the select-theme
                           // picker -- ProjectFindFile's fuzzy session shape over
                           // theme names, plus live preview of the highlighted
                           // candidate (see HandleSelectThemeKey below).
                           SelectTheme,
                           // prefix-argument follow-up: reading a C-u numeric
                           // argument -- same multi-keystroke session shape as
                           // Isearch*, driven by HandlePrefixArgumentKey via
                           // Editor/PrefixArgument.h's PrefixArgumentReader. A
                           // terminating (non-C-u/digit/"-") key exits this
                           // mode and is re-dispatched normally through
                           // DispatchChordNormally with pendingPrefixArg_
                           // applied.
                           PrefixArgument };

    enum class DeleteFileStage { EnteringPath,
                                 Confirming };
    enum class RenameFileStage { EnteringSource,
                                 EnteringDestination };
    // backup-and-recovery follow-up: recover-file's two linear stages,
    // DeleteFileStage's exact shape (pick, then y/n).
    enum class RecoverFileStage { PickingVersion,
                                  Confirming };
    // task-runner follow-up: which action TaskName's prompt performs on
    // Enter -- Run calls TaskRunner::RunTask (and switches to the resulting
    // buffer), Cancel calls TaskRunner::CancelTask.
    enum class TaskPromptAction { Run,
                                  Cancel };

    // Builds a fresh CommandContext from current member state -- matches
    // CommandContext's own documented contract ("built fresh per invocation
    // ... never stored"). Only used for the normal Dispatcher::Feed path;
    // the interactive sub-sessions below read/write buffer/statusMessage_
    // directly, since they don't go through CommandRegistry.
    [[nodiscard]] editor::CommandContext MakeContext();

    // Keyboard/mouse handling split out of OnEvent for readability -- was
    // key_press/mouse_press/mouse_move/mouse_release/mouse_wheel.
    bool OnKeyEvent(const Event& event);
    bool OnMouseEvent(const Event& event);

    void StartInteractiveSession(editor::InteractiveRequest request);
    void EndInteractiveSession();
    // The shared "feed one chord through Dispatcher::Feed, handle the
    // Pending/Unbound echo-area messages" tail every normal (InputMode::
    // Normal) keystroke goes through -- factored out so
    // HandlePrefixArgumentKey's terminating-key case can re-dispatch that
    // same chord through the identical path once a reading session ends.
    bool DispatchChordNormally(const editor::KeyChord& chord);
    void HandlePrefixArgumentKey(const editor::KeyChord& chord);
    void HandleSearchKey(const editor::KeyChord& chord);
    // search_->StatusText() plus a dimmed ghost of lastSearchQuery_ appended
    // when the current query is still empty -- see lastSearchQuery_'s own
    // doc comment.
    [[nodiscard]] std::string SearchStatusText() const;
    void                      HandleQueryReplaceKey(const editor::KeyChord& chord);
    void                      HandleConfirmQuitKey(const editor::KeyChord& chord);
    void                      HandlePromptKey(
        const editor::KeyChord&
            chord);        // shared by FindFile/SwitchToBuffer/ProjectSearch/CreateDirectory/FindScratch/StringRectangle -- see prompt_
    void CompletePrompt(); // Tab in HandlePromptKey -- find-file paths, buffer names, or scratch names, by inputMode_
    // minibuffer-history-recall follow-up: the short static key
    // promptHistory_ rings are recorded/recalled under for each of
    // HandlePromptKey's own InputModes -- only covers modes reachable
    // through HandlePromptKey (ExecuteCommand/ProjectFindFile use their own
    // literal keys directly, having no shared label switch to hang this
    // off of).
    [[nodiscard]] static std::string_view HistoryKeyForInputMode(InputMode mode);
    // M-p/M-n (Meta+p/Meta+n, no Control -- free to reuse here: ghost-text
    // completion cycling is the only other M-n/M-p binding, and it's scoped
    // to InputMode::Normal, which never reaches any prompt handler). Returns
    // false for any other chord (caller falls through to its own
    // Backspace/plain-character handling); true means prompt_'s text (and
    // the browsing cursor) was updated and the caller should refresh its own
    // status display and return. See promptHistoryIndex_/promptHistoryStash_
    // for the browsing-state fields this reads and mutates.
    [[nodiscard]] bool TryNavigatePromptHistory(const editor::KeyChord& chord, std::string_view key);
    void               HandleProjectReplaceKey(const editor::KeyChord& chord);
    void               HandleConfirmCloseBufferKey(const editor::KeyChord& chord);       // see RequestCloseBuffer/pendingClose_
    void               HandleConfirmOverwriteSaveKey(const editor::KeyChord& chord);     // external-modification-safety: y -> save-buffer-force
    void               HandleConfirmSaveWithConflictsKey(const editor::KeyChord& chord); // external-modification-round-2: y -> save-buffer-force
    void               HandleConfirmOpenBinaryKey(const editor::KeyChord& chord);        // see pendingBinaryOpenPath_
    void               HandleConfirmTrustProjectInitKey(const editor::KeyChord& chord);  // see pendingTrustInitPath_
    // Shared by HandlePromptKey's FindFile branch and the public
    // RequestOpenBinaryFile -- enters ConfirmOpenBinary and sets
    // pendingBinaryOpenPath_/statusMessage_.
    void BeginConfirmOpenBinary(const std::filesystem::path& path);

    // Both project-file-ops follow-up, both a simple two-stage flow driven
    // directly on BufferView (no dedicated state-machine class, unlike
    // QueryReplace/ProjectReplace -- these are linear with no branching
    // decision beyond the final y/n, closer in shape to
    // ConfirmCloseBuffer/pendingClose_ than to anything QueryReplace-sized):
    // HandleDeleteFileKey prompts for a path (deleteStage_ ==
    // EnteringPath), then, once it's confirmed to exist, re-purposes
    // statusMessage_ for a y/n confirmation (deleteStage_ == Confirming) --
    // mirroring HandleConfirmCloseBufferKey/HandleConfirmQuitKey's own
    // y/n shape exactly, since deleting a file is just as irreversible.
    // HandleRenameFileKey prompts for the source path (renameStage_ ==
    // EnteringSource), then re-emplaces prompt_ for the destination
    // (renameStage_ == EnteringDestination) and performs the rename on the
    // second Enter; if the renamed file is the currently active buffer,
    // Buffer::SetPath/Rename follow it to the new location rather than
    // leaving that buffer pointing at a now-nonexistent path.
    void HandleDeleteFileKey(const editor::KeyChord& chord);
    void HandleRenameFileKey(const editor::KeyChord& chord);

    // backup-and-recovery follow-up: recover-file's session, the same
    // linear no-state-machine-class shape as HandleDeleteFileKey just
    // above. PickingVersion collects a 1-based number into prompt_ (the
    // candidates were listed in statusMessage_ by StartInteractiveSession;
    // Enter alone means 1, the newest), then Confirming re-purposes
    // statusMessage_ for the final y/n; y reads the chosen snapshot and
    // restores it via Buffer::RestoreContent -- one undoable step, buffer
    // left Modified() so only an explicit save makes the recovery stick.
    void HandleRecoverFileKey(const editor::KeyChord& chord);

    // code-actions follow-up. RequestCodeActionsAtPoint mirrors
    // RequestCompletionAtPoint's shape: finds the diagnostic covering point
    // (same lookup lsp-show-diagnostic already does against
    // Buffer::Diagnostics()) or falls back to a zero-length range at point,
    // bumps codeActionRequestGeneration_, and calls
    // LspManager::RequestCodeActions. The callback (capturing a raw
    // Buffer* for pointer-value-only comparison, point, and generation --
    // same idiom RequestCompletionAtPoint already uses) discards a stale
    // response (generation moved, or buffer/point changed since the request
    // was sent) rather than surprising the user with a selection prompt for
    // something they've since moved on from; otherwise sets
    // pendingCodeActions_ and enters LspCodeActionConfirm directly (exactly
    // one action) or LspCodeActionSelect (more than one) -- inputMode_ is
    // therefore only ever touched from inside this async callback, never
    // eagerly when the request is first sent.
    void RequestCodeActionsAtPoint();
    // Renders pendingCodeActions_ as a numbered list into statusMessage_,
    // codeActionSelection_ visually marked -- called after RequestCodeActionsAtPoint
    // first enters LspCodeActionSelect, and again by HandleCodeActionSelectKey
    // whenever Up/Down changes the selection.
    void RefreshCodeActionSelectStatus();
    // Up/Down move codeActionSelection_ (clamped) and refresh; a digit '1'-'9'
    // jumps directly to that index (clamped to the available count) and falls
    // through to the same LspCodeActionConfirm transition Enter performs;
    // Escape/C-g cancels back to Normal.
    void HandleCodeActionSelectKey(const editor::KeyChord& chord);
    // y/Y applies pendingCodeActions_[codeActionSelection_] via
    // ApplyCodeAction and ends the session; n/N/Escape/C-g cancels --
    // mirrors HandleDeleteFileKey's own Confirming-stage shape exactly.
    void HandleCodeActionConfirmKey(const editor::KeyChord& chord);
    // ACP client slice 2: same numbered-list rendering as
    // RefreshCodeActionSelectStatus, over pendingAcpPermissionOptions_/
    // acpPermissionSelection_ instead -- called by ShowAcpPermissionPrompt
    // and again by HandleAcpPermissionPromptKey whenever Up/Down changes
    // the selection.
    void RefreshAcpPermissionPromptStatus();
    // Up/Down move acpPermissionSelection_ (clamped) and refresh; a digit
    // '1'-'9' or Enter resolves the (possibly just-picked) option directly
    // via AcpManager::ResolvePermissionPrompt -- unlike a code action,
    // there is no separate confirm stage: the options an agent offers are
    // already the concrete choices ("Allow once", "Reject", ...), not a
    // list of actions needing a second y/n. Escape/C-g resolves as
    // cancelled via AcpManager::CancelPermissionPrompt.
    void HandleAcpPermissionPromptKey(const editor::KeyChord& chord);
    // Refuses (reports via statusMessage_, no buffer mutation) if
    // action.touchesOtherFiles or it has no edit to apply. Otherwise
    // resolves each WorkspaceTextEdit's LspPositions to byte offsets against
    // the buffer's CURRENT content (safe without a fresh generation check --
    // the modal Select/Confirm input modes already block ordinary
    // typing/editing for the whole exchange), sorts the resolved edits
    // descending by start byte (so an edit not yet applied keeps a valid
    // offset as an earlier-in-the-buffer one shifts positions), then applies
    // each via Buffer::DeleteRange + Buffer::InsertAt.
    void ApplyCodeAction(const editor::lsp::CodeAction& action);
    // code-actions-resolve follow-up (factored out for quick-fix). Sends
    // codeAction/resolve first when the action arrived without its edit
    // (action.resolvable), applying from inside that async callback;
    // otherwise applies directly. Shared by HandleCodeActionConfirmKey's y
    // branch and RequestQuickFixAtPoint.
    void ResolveAndApplyCodeAction(const editor::lsp::CodeAction& action);
    // quick-fix follow-up. Same request/staleness-guard shape as
    // RequestCodeActionsAtPoint, but applies the response's single
    // unambiguous fix immediately (a lone action, else a lone isPreferred
    // one, else a lone quickfix-kind one -- undo is the safety net, per the
    // user's own ask), entering the ordinary LspCodeActionSelect session
    // only when no selector produces exactly one candidate.
    void RequestQuickFixAtPoint();

    // go-to-definition follow-up. Mirrors RequestCodeActionsAtPoint's own
    // shape exactly: bumps definitionRequestGeneration_, calls
    // LspManager::RequestDefinition, and discards a stale response (buffer/
    // point changed, or a newer request already superseded it) the same
    // way. Zero locations reports "No definition found." via
    // statusMessage_; exactly one jumps directly (JumpToDefinition, no
    // confirmation needed -- unlike a code action, opening a file and
    // moving point is trivially undoable/re-navigable, nothing destructive
    // to confirm); more than one enters LspGotoDefinitionSelect the same
    // way multiple code actions enter LspCodeActionSelect.
    void RequestDefinitionAtPoint();
    void RefreshDefinitionSelectStatus();
    void HandleDefinitionSelectKey(const editor::KeyChord& chord);
    // Opens location.path (BufferList::OpenOrCreateFile, matching
    // VisitSearchResult's own precedent for jumping into a project file)
    // and moves point to location.position, resolved against the newly-
    // opened buffer's own content.
    void JumpToDefinition(const editor::lsp::LspManager::ResolvedLocation& location);

    // header-source-switching follow-up. Bumps
    // switchHeaderSourceRequestGeneration_ and calls LspManager::
    // RequestSwitchSourceHeader (clangd's own custom LSP extension) when an
    // LspManager is set; nullopt from that -- no client running, server has
    // no counterpart to offer, or LSP unavailable at all -- falls through to
    // OpenHeaderSourceCounterpartOrReport's Editor/HeaderSource.h filesystem
    // heuristic, unlike RequestDefinitionAtPoint's own LSP-required "No LSP
    // manager available." refusal: LSP is a nice-to-have accelerant here,
    // not the only path, since every non-C/C++ language reaches the
    // heuristic unconditionally.
    void SwitchHeaderSource();
    // Shared tail: tries the filesystem heuristic and reports "No
    // corresponding header/source file found." on failure, opening the
    // counterpart via OpenHeaderSourceCounterpart on success.
    void OpenHeaderSourceCounterpartOrReport(const std::filesystem::path& path);
    // Opens path (BufferList::OpenOrCreateFile) and switches to it -- no
    // position to restore, unlike JumpToDefinition, since a header/source
    // counterpart has no natural corresponding point.
    void OpenHeaderSourceCounterpart(const std::filesystem::path& path);

    // rename follow-up. StartInteractiveSession's LspRename case opens the
    // synchronous "New name: " prompt (inputMode_ = LspRenameNewName);
    // HandlePromptKey's own Enter branch for that mode calls this once the
    // name is typed. Mirrors RequestCodeActionsAtPoint's async/staleness-
    // guard shape once again, but resolves to a full ResolvedRename
    // (potentially many files) rather than a single buffer's edits.
    void RequestRenameAtPoint(const std::string& newName);
    void RefreshRenameConfirmStatus();
    void HandleRenameConfirmKey(const editor::KeyChord& chord);
    // Refuses (statusMessage_, no mutation anywhere) if
    // result.touchesUnsupportedForm or it has no edit at all. Otherwise
    // opens/finds every touched file first (BufferList::FindByPath, else
    // BufferList::OpenFile) and bails out -- applying nothing -- the moment
    // any one of them fails to open, so a rename either fully applies
    // across every file or leaves every buffer untouched, never a partial
    // rename across only some of the affected files. Once every buffer is
    // confirmed open, applies each buffer's own edits via the same
    // resolve-LspPositions-against-current-content + descending-sort-by-
    // start-byte + DeleteRange/InsertAt sequence ApplyCodeAction already
    // established -- factored into a shared ApplyWorkspaceTextEdits helper
    // (BufferView.cpp, file-local) both now call. Every affected buffer is
    // left modified-but-unsaved, exactly like any other in-editor edit --
    // no auto-save-across-files behavior, matching this codebase's existing
    // "saves are always user-initiated" convention.
    void ApplyRename(const editor::lsp::LspManager::ResolvedRename& result);

    // point-to-register/jump-to-register/copy-to-register/insert-register
    // follow-up: one shared method for all four (mirrors HandlePromptKey's
    // own "several related modes, one handler that switches on inputMode_
    // internally" shape) rather than four near-duplicate methods, since each
    // operation's own interaction shape is identical -- read exactly one
    // more character (the register name, no MinibufferPrompt needed, there's
    // nothing to accumulate), act, end the session. Only what happens with
    // that character differs per inputMode_. See Editor/Register.h for where
    // the actual register storage lives (registers_ below).
    void HandleRegisterKey(const editor::KeyChord& chord);

    // Emacs-keymap-round-2 follow-up: zap-to-char's own one-character-read
    // session (same shape as HandleRegisterKey above) -- scans forward from
    // point for the read character and kills up to and including it,
    // per-cursor when secondary cursors are active (KillPerCursor's own
    // "one piece per cursor, empty for no match" resolution). See
    // pendingZapToCharAppend_'s own doc comment for the kill-append hint
    // this reads.
    void HandleZapToCharKey(const editor::KeyChord& chord);

    // execute-extended-command follow-up (M-x): given its own dedicated
    // method rather than folded into HandlePromptKey, since HandlePromptKey's
    // Enter-branch has an unconditional catch-all else currently reached only
    // by FindScratch -- silently misrouting a new mode into it would be a
    // real bug -- and because its key semantics (Up/Down candidate
    // selection, re-ranking on every keystroke) genuinely differ from every
    // HandlePromptKey mode's shared shape, the same reason
    // DeleteFile/RenameFile already get their own methods. Prompts for a
    // command name, fuzzy-matched (Editor/FuzzyMatch.h) against
    // dispatcher_.Registry().Names() and re-ranked on every keystroke; Enter
    // invokes whichever ranked candidate is currently selected. Shown inline
    // via statusMessage_ using the same "label + text + {candidates}"
    // convention CompletePrompt already established, since this codebase has
    // no floating/popup widget concept to show a real dropdown in (see
    // ProjectSidebar's own context-menu descoping history).
    void HandleExecuteCommandKey(const editor::KeyChord& chord);

    // Refreshes statusMessage_ from the current prompt_ text and
    // executeCommandSelection_ -- shared by StartInteractiveSession's
    // ExecuteCommand case and every branch of HandleExecuteCommandKey that
    // changes either one.
    void RefreshExecuteCommandStatus();

    // fuzzy-candidate-list-styling follow-up: how many columns
    // FormatFuzzyCandidates' candidate list actually has to work with --
    // this widget's own current width (size().width, the same live value
    // OnKeyEvent already reads into context.viewportHeight for
    // scroll-page-up/-down) minus prefixLength (the already-composed
    // "<label>  {" that precedes the candidate list in statusMessage_) and
    // one more column for the closing "}". EchoArea itself spans the full
    // terminal width, not just this BufferView's own (narrower, once a
    // sidebar/scrollbar/gutter are subtracted) box -- using this widget's
    // own width anyway is a deliberate, safe approximation: it can only
    // under-estimate the real budget, which means the candidate list might
    // occasionally show fewer entries than would actually fit, never more
    // than actually fit (which is what caused the real overflow this
    // follow-up fixes). Getting the exact real width would mean new
    // BufferView<->EchoArea wiring for a one-row status list; not worth it
    // for a safe-direction approximation that's already correct in the only
    // direction that matters (never overflowing).
    [[nodiscard]] std::size_t AvailableCandidateColumns(std::size_t prefixLength) const;

    // project-find-file follow-up: same shape as HandleExecuteCommandKey/
    // RefreshExecuteCommandStatus just above, but fuzzy-matching against a
    // cached list of project-relative file paths (projectFindFileCandidates_,
    // populated once when the session starts -- unlike
    // dispatcher_.Registry().Names(), a real recursive directory walk
    // (editor::BuildProjectTree) is too expensive to redo on every
    // keystroke) instead of dispatcher_.Registry().Names(), and opening the
    // selected file (BufferList::OpenOrCreateFile + ActiveBuffer::Set,
    // mirroring HandlePromptKey's own FindFile branch) on Enter instead of
    // invoking a command by name.
    void HandleProjectFindFileKey(const editor::KeyChord& chord);
    void RefreshProjectFindFileStatus();

    // rich-theme-set follow-up (Phase 1): same shape as
    // HandleProjectFindFileKey/RefreshProjectFindFileStatus just above but
    // over ui::ThemeNames() (cached per session like the file list, though
    // only for symmetry -- it's a cheap in-memory table), with one genuine
    // addition: every selection/rank change also routes through
    // ApplySelectedThemePreview, so the highlighted theme is what the whole
    // UI is showing before Enter ever commits it. Enter keeps the previewed
    // theme and reports it; Escape/C-g re-applies themeBeforePreview_ (a
    // full Theme snapshot copied from theme_ at session start -- a copy,
    // not a name, so cancelling restores exactly what was showing even if
    // the active theme never came from the registry at all, e.g. a
    // --detect-theme file).
    void HandleSelectThemeKey(const editor::KeyChord& chord);
    void RefreshSelectThemeStatus();
    void ApplySelectedThemePreview();

    // Shared by OnKeyEvent's Normal-mode tail (Dispatcher::Feed) and
    // HandleExecuteCommandKey's Enter branch (CommandRegistry::Invoke by
    // name): applies the two dispatch-level side effects a command can
    // request -- context.quit (exit the app) and a chained
    // context.interactiveRequest (immediately start that request's own
    // session, letting an M-x-invoked command like find-file chain straight
    // into its own prompt) -- and catches std::exception into
    // statusMessage_, regardless of how the command was found. invoke is the
    // actual dispatch call, run inside the try.
    // structural-selection-expansion follow-up: invoke now returns whether a
    // command actually ran (true) as opposed to merely extending a pending
    // multi-chord prefix sequence (false, e.g. the bare Escape half of an
    // "ESC =" two-chord binding) -- expansionHistory_'s own clearing logic
    // below needs that distinction, since a Dispatcher::Outcome::Pending
    // call leaves context.interactiveRequest at None exactly the same as a
    // real command that simply doesn't set it, and only the latter should
    // count as "something else happened."
    // hover/completion follow-up: triggeringChord is non-null only from
    // OnKeyEvent's own normal-dispatch call site -- ReplayMacro/
    // HandleExecuteCommandKey's calls leave it at the default nullptr, so
    // macro replay and M-x-invoked commands never schedule an automatic
    // completion request (only organic, direct keystrokes do). Used, when
    // present, only in the ordinary (interactiveRequest == None) tail below,
    // the one branch already guaranteed *this* is still alive.
    // status-message-lifecycle follow-up: now returns the same `ran` this
    // already computed internally (true iff a command actually ran, even
    // if it threw) -- OnKeyEvent's own normal-dispatch call site needs it
    // to know whether Dispatcher::Feed's returned Outcome is trustworthy
    // for deciding whether to show a pending-sequence/undefined-key
    // message: when a Match'd command throws, Feed itself never reaches
    // its own `return Outcome::Invoked`, so the Outcome captured at that
    // call site is stale (still whatever it was default-initialized to) --
    // checking `ran` first, not the possibly-stale Outcome, is what avoids
    // clobbering the exception's own message with a bogus "X is undefined"
    // (a real bug, caught by this session's own pre-existing exception-
    // handling test, not hypothetical).
    bool RunCommandAndHandleOutcome(editor::CommandContext& context, const std::function<bool()>& invoke,
                                    const editor::KeyChord* triggeringChord = nullptr);

    // kmacro-end-or-call-macro follow-up: replays dispatcher_.LastMacro(),
    // one chord at a time, each through a fresh MakeContext() +
    // RunCommandAndHandleOutcome -- exactly what a real keystroke does.
    // Reports "No keyboard macro has been recorded yet." via statusMessage_
    // if nothing's been recorded (mirrors delete-window's own "Cannot delete
    // the only window." can't-do-that-right-now convention). Stops early,
    // leaving the rest of the macro un-replayed, the instant a replayed
    // command opens an interactive session (inputMode_ != Normal) or
    // requests quit -- a macro's own recording never captured what happens
    // *inside* such a session (see Dispatcher::StartRecording's own doc
    // comment), so blindly continuing to feed the macro's remaining chords
    // through Dispatcher::Feed underneath a now-live session would be
    // feeding them to the wrong place entirely; this leaves that session
    // genuinely live for the user to finish by hand instead.
    void ReplayMacro();

    // narrow-to-region/widen follow-up: keeps point confined to a narrowed
    // buffer's own NarrowedRange() -- a no-op if the active buffer isn't
    // narrowed. Called once before each of OnKeyEvent's own return
    // statements, and once per chord inside ReplayMacro's own loop -- these
    // are the two real entry points for anything that could move point
    // (every key-driven Handle*Key method, plus macro replay, which
    // bypasses OnKeyEvent entirely). Most of Buffer's own motion methods
    // mutate Point_ by direct assignment rather than through the one public
    // SetPoint() setter (confirmed by reading Buffer.cpp directly), so
    // clamping only inside SetPoint would silently miss most of them --
    // this is the actual, correct, centralized place instead. Deliberately
    // not inside Paint(): giving rendering code a buffer-mutating side
    // effect would make repeated Paint() calls with no intervening input
    // non-idempotent.
    void ClampPointToNarrowing();

    // The actual close: removes buffer from bufferList_ and, if it was the
    // active one, switches activeBuffer_ to whatever remains (the first
    // other buffer in list order -- there is no "most recently used" concept
    // to prefer here yet). Caller (RequestCloseBuffer or
    // HandleConfirmCloseBufferKey) is responsible for the modified-buffer
    // confirmation decision; this always closes unconditionally.
    void CloseBufferNow(text::Buffer& buffer);

    // Builds a results buffer (path:line: text per line, name uniquified
    // Emacs-style like any other buffer) from matches and switches to it --
    // shared by project-search's own results view and project-replace's
    // preview, which deliberately shows the same file/line detail rather
    // than a bare count (see ROADMAP.md's project-replace notes).
    void BuildResultsBuffer(const std::vector<editor::SearchMatch>& matches, const std::string& name);

    // VisitSearchResult (project-search follow-up): a one-shot direct action,
    // not a prompt session -- doesn't touch inputMode_. Parses the current
    // line for a "path:line:" prefix (the exact format project-search writes
    // into its results buffer) and, if it matches, opens that file and jumps
    // to the target line. A silent no-op on any line that doesn't match,
    // which is what makes it safe to bind globally rather than gating it on
    // which buffer happens to be active.
    void VisitSearchResult();

    // Shared by VisitSearchResult and VisitVcsResult below -- declared in
    // the public section instead (see its own comment there); kept
    // referenced here so the two Visit* methods' doc comments still point
    // somewhere true.

    // VCS blame gutter follow-up: same "path:line:" prefix VisitSearchResult
    // parses (see BuildVcsBlameBuffer's own doc comment for why the format
    // is deliberately kept byte-compatible), via the shared JumpToPathLine
    // above. A silent no-op on a non-matching line -- e.g. every line of a
    // *vcs log* buffer, which has no per-line source location -- same
    // convention VisitSearchResult already established.
    void VisitVcsResult();

    // vcs-blame-buffer/vcs-show-log's actual entry points (see
    // StartInteractiveSession's VcsBlameBuffer/VcsShowLog cases) -- resolve
    // the active buffer's path, kick off an async VcsRunner request, and on
    // completion (which may arrive after further user input -- checked via
    // the same buffer-identity staleness guard RequestBlameForCurrentBuffer
    // already uses) build and switch to a synthesized results buffer. The
    // full-history views, not vcs-show-blame's own default action anymore
    // (see InteractiveRequest::VcsShowBlame's own doc comment in Command.h
    // for why) -- RequestVcsBlameBuffer still also populates the gutter for
    // the source buffer along the way, same as RequestBlameForCurrentBuffer
    // alone would, before switching away from it.
    void RequestVcsBlameBuffer();
    void RequestVcsLogBuffer();

    // vcs-blame-detail-at-point's entry point: a synchronous read of
    // already-loaded blameLineInfo_ for the buffer line at point -- no new
    // VcsRunner request. Reports the full commit hash/author/date/summary
    // via statusMessage_ (the gutter's own fixed-width column only ever
    // shows a short hash), or a clear "no blame data" message if
    // BlameGutterActive() is false or point's line isn't covered (e.g. an
    // unsaved/uncommitted line with no attribution yet).
    void ShowBlameDetailAtPoint();

    // Diff gutter markers follow-up: (re)arms diffRefreshTimer_ for
    // editor::DiffRefreshDebounce() -- called from RunCommandAndHandleOutcome after
    // any dispatch that actually changed buffer content, so rapid typing
    // keeps pushing the deadline out rather than spawning a `git diff` per
    // keystroke (the exact debounce shape completionDebounceTimer_ already
    // established for LSP ghost-text completion). A no-op if no
    // EventLoop/VcsRunner is wired in (headless tests, most notably).
    void ScheduleDiffRefresh();
    // The actual request, called either from ScheduleDiffRefresh's fired
    // timer or immediately after a save (RunCommandAndHandleOutcome
    // bypasses the debounce there -- a save is a natural "make this fresh
    // now" moment, no reason to wait). Silent on any failure (no path, no
    // provider, process failure) -- this is a best-effort background
    // feature, not a user-invoked action, so it must never interrupt with
    // a status message the way vcs-show-blame's own errors do.
    void RequestDiffForCurrentBuffer();

    // Parallel to BuildResultsBuffer, just a different per-line text shape:
    // "<path>:<1-indexed line>: <hash> <author> <date> | <source line
    // text>" -- deliberately kept byte-compatible with VisitSearchResult's
    // own "^(.*):(\d+):" prefix parsing (see JumpToPathLine) so a blame
    // line can be visited the same way a search result can. Buffer name is
    // "*vcs blame <basename>*" -- generic, not "*git ...*", since the
    // active provider might not be git.
    void BuildVcsBlameBuffer(const std::filesystem::path& path, const std::vector<editor::vcs::VcsBlameLine>& lines);
    // One line per commit ("<hash> <date> <author>: <summary>"), oldest-to-
    // newest order preserved as returned by the provider. Log entries don't
    // map to a specific source line -- VisitVcsResult on one of these lines
    // is a silent no-op, same as any other non-matching line.
    void BuildVcsLogBuffer(const std::filesystem::path& path, const std::vector<editor::vcs::VcsLogEntry>& entries);

    // Multibuffers follow-up: vcs-full-diff-buffer's entry point -- async
    // VcsRunner::RequestFullDiff, then Vcs/DiffPatch.h's ParseDiffHunks and
    // Editor/Multibuffer.h's BuildMultibuffer turn the raw diff text into a
    // real, stitched "*vcs diff*" buffer (one excerpt per hunk, each
    // carrying its own file/line provenance for vcs-visit-result to jump
    // to). Empty result (a clean working tree) still switches to the
    // buffer -- an explicit "nothing changed" is more informative than a
    // silent no-op.
    void RequestVcsFullDiffBuffer();

    // Diagnostics-multibuffer follow-up: lsp-diagnostics-buffer's entry
    // point -- synchronous (every diagnostic is already resident on its own
    // open Buffer, no VcsRunner-style subprocess round trip needed), unlike
    // RequestVcsFullDiffBuffer's async shape. Builds one excerpt per
    // Code-origin diagnostic (its own single source line, verbatim) via
    // Editor/Multibuffer.h's BuildMultibuffer, then translates each
    // diagnostic's original (per-source-buffer) byte range into the
    // composite buffer's own byte space and re-applies it via
    // Buffer::SetDiagnostics -- deliberately not a new LineTint (unlike
    // vcs-full-diff-buffer's Added/Removed wash): reusing the composite
    // buffer's own real Diagnostic entries means the ordinary diagnostic
    // gutter glyph, underline, severity color, and inline-annotation row
    // every other diagnostic view already draws light up unmodified, no new
    // rendering path or hardcoded color needed.
    void RequestDiagnosticsBuffer();

    // find-all-references follow-up: project-find-references's entry point
    // -- synchronous, same shape as RequestDiagnosticsBuffer (SearchDirectory
    // is now an in-process RE2 scan, no subprocess round trip). Finds the
    // ASCII word/identifier region at point (a local scan, same
    // classification as Commands.cpp's own WordRegionAt -- see that
    // function's doc comment for why this is a small duplicated copy rather
    // than a new shared seam), builds a whole-word pattern ("\\bword\\b" --
    // safe to embed unescaped since the word-scan only ever admits
    // [A-Za-z0-9_], none of them regex metacharacters), and runs it through
    // SearchDirectory(ProjectRoot(), ...). One excerpt per matching line
    // (SearchMatch::lineText, no extra context) stitched into
    // "*references: <word>*" via Editor/Multibuffer.h's BuildMultibuffer --
    // vcs-visit-result already jumps to source from any excerpt, same as
    // RequestVcsFullDiffBuffer/RequestDiagnosticsBuffer's own buffers. A
    // fast textual approximation (matches inside comments/strings too, and
    // can't tell one same-named symbol from another in a different scope) --
    // not real semantic LSP references, which don't exist as a client
    // capability yet at all (see ROADMAP.md).
    void RequestProjectFindReferences();

    // VCS vocabulary-completion follow-up. vcs-status's entry point --
    // async VcsRunner::RequestStatus, building/switching to the *vcs
    // status* buffer on completion.
    void RequestVcsStatusBuffer();
    // "<absolute path>:1: <state> <root-relative path>" per entry -- the
    // ":1:" is deliberate VisitSearchResult/JumpToPathLine byte-
    // compatibility (a status entry has no line number; 1 visits the top
    // of the file), the same convention BuildVcsBlameBuffer documents.
    // Unlike the per-file blame/log buffers, the status buffer is a
    // root-scoped singleton refreshed *in place* (Find-first, refill,
    // point clamped -- the LspShowLog find-or-create precedent) rather
    // than accumulating uniquified copies per invocation, because
    // stage/unstage/commit re-trigger it programmatically. announce=false
    // is the background-refresh variant: no buffer switch, no
    // statusMessage_ (so it can't clobber "Staged foo"'s own report).
    void BuildVcsStatusBuffer(const std::vector<editor::vcs::VcsStatusEntry>& entries, bool announce);
    // Background re-request after a stage/unstage/commit/branch-switch
    // changed what status would report -- a no-op unless the *vcs status*
    // buffer already exists (never conjures one unasked), with errors
    // swallowed, the diff gutter's own silent-degrade convention.
    void RefreshVcsStatusBuffer();
    // vcs-stage-file/vcs-unstage-file's shared entry point: resolves the
    // target via ResolveVcsFileTarget, fires the async request, and on
    // success refreshes the status buffer + diff gutter (staging moves a
    // file's changes into the index, which is exactly what the bundled
    // git plugin's worktree-vs-index diff stops reporting).
    void StageOrUnstageFileAtPoint(bool stage);
    // Hunk-staging follow-up (vcs-stage-hunk/vcs-unstage-hunk): hands
    // point's 1-indexed line to VcsRunner::RequestHunkApply after three
    // synchronous gates -- a runner is wired, the buffer has a path, and
    // the buffer is NOT Modified(): the diff describes the file on disk
    // while point counts buffer lines, so staging from mismatched numbers
    // would silently pick the wrong hunk; "save first" is the honest
    // answer, not a limitation to paper over. Success refreshes the status
    // buffer + diff gutter, same as the whole-file pair above. (An unstage
    // additionally assumes index and worktree line numbers agree for the
    // hunk's region -- true in the common stage-then-oops flow; drift from
    // *earlier* unstaged edits in the same file is a recorded caveat, see
    // ROADMAP.md.)
    void StageOrUnstageHunkAtPoint(bool stage);
    // multi-line-commit-message follow-up: opens (or, if one's already
    // mid-composition, just switches to) the *vcs commit message* buffer --
    // InteractiveRequest::VcsCommit's entry point.
    void BeginVcsCommitMessage();
    // InteractiveRequest::VcsCommitFinish/VcsCommitAbort's entry points --
    // strip the '#'-comment template and fire RequestCommit, or just
    // discard, then either way close the buffer via
    // CloseVcsCommitMessageBuffer below.
    void FinishVcsCommitMessage();
    void AbortVcsCommitMessage();
    // Shared by both: closes the commit-message buffer via CloseBufferNow
    // (bypassing RequestCloseBuffer's "unsaved changes?" prompt -- finishing
    // or aborting the commit already IS the user's confirmation) and
    // best-effort removes its backing temp file.
    void CloseVcsCommitMessageBuffer(text::Buffer& commitBuffer);
    // The file a stage/unstage acts on: in the *vcs status* buffer, the
    // "<path>:1:" prefix of the line at point (VisitVcsResult's own
    // parse); anywhere else, the active buffer's associated path.
    // nullopt if neither yields one.
    [[nodiscard]] std::optional<std::filesystem::path> ResolveVcsFileTarget();
    // vcs-branches' entry point + its "* current / plain other" one-line-
    // per-branch buffer -- also a Find-first in-place singleton, same
    // reasoning as the status buffer.
    void RequestVcsBranchesBuffer();
    void BuildVcsBranchesBuffer(const std::vector<editor::vcs::VcsBranchEntry>& entries);
    // vcs-switch-branch's entry point: fetches the branch list first and
    // only then -- from the async callback, the RequestRenameAtPoint
    // enter-a-mode-from-a-callback pattern -- opens the
    // InputMode::VcsSwitchBranch prompt, with the fetched names parked in
    // vcsBranchCandidates_ for Tab completion. Dropped (with a status
    // message) if another prompt began while the fetch was in flight.
    void BeginVcsSwitchBranchPrompt();

    // Links follow-up: another one-shot direct action, same shape as
    // VisitSearchResult -- doesn't touch inputMode_. In an org-mode buffer,
    // tries org::LinkAtPoint first (an internal "*Heading" target jumps
    // point in-buffer via org::FindHeadlineByTitle; any other target is
    // classified and handed to OpenDetectedLink below, reusing the same
    // logic the generic path uses); falls back to (or, outside org-mode,
    // goes straight to) editor::link::DetectLinkAtPoint. Reports "No link at
    // point." via statusMessage_ if nothing is found either way.
    void OpenLinkAtPoint();
    // The shared open/report tail both OpenLinkAtPoint paths above funnel
    // into: a Url opens via editor::link::OpenUrl; a File is resolved via
    // editor::link::ResolveFileLink against the active buffer's own
    // containing directory (falling back to editor::ProjectRoot() when the
    // buffer has no path, e.g. a scratch buffer) and, if found, opened the
    // same way VisitSearchResult opens a file (bufferList_.OpenOrCreateFile +
    // activeBuffer_.Set).
    void OpenDetectedLink(const editor::link::DetectedLink& detected);

    // Adjusts the viewport (if needed) so point's line is visible. A thin
    // wrapper over ScrollToShowOffset(activeBuffer_.Get().Point()) plus the
    // horizontal counterpart below.
    void ScrollToShowPoint();

    // multi-cursor-round-2 follow-up: ScrollToShowPoint()'s real (vertical
    // only -- no horizontal counterpart, a deliberate scope cut) logic,
    // parameterized on an explicit byte offset instead of always reading
    // activeBuffer_.Get().Point() -- lets RunCommandAndHandleOutcome scroll
    // to a newly added secondary cursor (add-cursor-above/-below,
    // select-next-occurrence) instead of the unmoved primary point, via
    // context.newlyAddedCursorPoint.
    void ScrollToShowOffset(std::size_t offset);

    // line-wrap follow-up: horizontal counterpart to ScrollToShowPoint(),
    // called alongside it -- a no-op whenever the active buffer's
    // EffectiveWrapLines() is true, since a wrapped line never extends past
    // the viewport width in the first place (confirmed by construction: see
    // ComputeWrapSegments's own doc comment). Adjusts leftColumn_ the same
    // "clamp to keep point visible, minimal movement" way
    // ScrollToShowPoint() adjusts topLine_.
    void ScrollToShowPointHorizontally();

    // Re-validates topLine_ via ScrollToShowPoint() whenever the active
    // buffer's identity has changed since the last call -- see
    // topLineValidatedBuffer_'s own doc comment for why this exists.
    // Deliberately doesn't reset topLine_ to 0 first: ScrollToShowPoint()
    // is already safe to call with a stale, possibly out-of-range topLine_
    // left over from the previous buffer (its own "point is above topLine_"
    // branch handles that unconditionally), and leaving topLine_ untouched
    // when it happens to already show the new buffer's point is a nicer
    // switch between two similarly long buffers than always jumping back to
    // the top. Called first thing in Paint(), before topLine_ is used for
    // anything.
    void EnsureTopLineValidForActiveBuffer();

    // The largest valid topLine_: the buffer's last line stops exactly at
    // the bottom of the viewport rather than scrolling past it into blank
    // filler rows. Used by both SetTopLine and Paint()'s scroll-bar sync, so
    // wheel/scroll-bar-driven scrolling and the bar's own visual range agree
    // on where "the bottom" is. narrow-to-region/widen follow-up: when the
    // active buffer is narrowed, this is computed against the narrowed
    // range's own line span instead of the whole buffer -- see
    // NarrowedLineRange.
    [[nodiscard]] std::size_t MaxTopLine() const;

    // narrow-to-region/widen follow-up: {0, Content().LineCount()} if the
    // active buffer isn't narrowed, otherwise the narrowed range's own
    // [startLine, endLine) span (endLine exclusive) -- shared by MaxTopLine,
    // SetTopLine, and Paint()'s own "blank past this line" cutoff so all
    // three agree on exactly the same bounds.
    [[nodiscard]] std::pair<std::size_t, std::size_t> NarrowedLineRange() const;

    // Translates an on-screen (LOCAL to this widget) mouse position into a
    // buffer byte offset, accounting for the current scroll position and the
    // line-number gutter.
    [[nodiscard]] std::size_t ByteOffsetForPoint(Point at) const;

    // Width in columns of the line-number gutter (digits needed for the
    // buffer's last line number, plus one separating column). Always
    // present -- there's no toggle to hide it yet.
    [[nodiscard]] std::size_t GutterWidth() const;

    // read-only-buffers follow-up: the one shared condition for "does the
    // active buffer get a fold-depth gutter at all" -- mode_.fold and
    // editor::CodeFoldingEnabled() were already checked (independently, in
    // four separate places: EnsureFoldableBlocksCache, Paint()'s own
    // gutter-width math, GutterWidth(), and OnMouseEvent's fold-click hit
    // test) before a synthesized, read-only buffer (project-search
    // results, project-replace's preview, project-agenda) could ever
    // exist; a real Mode's own fold query run against that buffer's own
    // "path:line: text" content produces meaningless fold regions, not an
    // empty result, so ReadOnly() has to be part of this condition too,
    // not just mode_.fold/CodeFoldingEnabled() -- centralized here so all
    // four call sites can't drift out of agreement with each other the way
    // duplicating a fourth inline copy of this check risked.
    [[nodiscard]] bool FoldGutterActive() const;

    // Diff gutter markers follow-up: same "only reserve the column when
    // there's something to show" gate BlameGutterActive() established --
    // true once diffLineKinds_ has any entries at all (a clean file
    // against HEAD reserves no column).
    [[nodiscard]] bool DiffGutterActive() const;

    // gutter-symbol-kind follow-up: self-ensuring, same shape as
    // DapGutterActive() below -- calls EnsureSymbolGutterCache() itself
    // (cheap after the first call per Paint(), generation-gated) rather than
    // relying on a separate unconditional Ensure* call elsewhere in Paint()
    // the way EnsureDiagnosticGutterCache/EnsureUnsavedChangeCache/
    // EnsureBlameGutterCache are -- unlike those three (async- or
    // externally-driven data an XGutterActive() check can safely read
    // slightly stale), symbolGutterLineKinds_ is synchronous, recomputed
    // straight from mode_.symbolKind on every genuine content change, so it
    // must be current *within this same Paint() call* the moment the
    // column-width math runs, not just by the time the render loop gets to
    // it a few hundred lines later. Same "only reserve the column when
    // there's something to show" gate DiffGutterActive/BlameGutterActive
    // use, not FoldGutterActive's fixed-width-regardless-of-content
    // reservation -- a symbol landmark on every definition line in a large
    // file is common enough that "no functions in this file" should cost
    // zero gutter width, not reserve a column nobody needs (the packed-
    // gutter concern this whole follow-up was built around).
    [[nodiscard]] bool SymbolGutterActive() const;

    // DAP client slice 2: whether the leftmost debug-marker column
    // (breakpoint dot / execution arrow) is reserved this frame -- true
    // only when the active buffer has a real path AND (it has breakpoints,
    // or the debuggee is currently stopped in it). Same "reserved only when
    // there's something to show" gate BlameGutterActive/DiffGutterActive
    // use. Cheap per call: the buffer's normalized path key is cached by
    // EnsureDapPathKey below, so no filesystem canonicalization happens per
    // frame.
    [[nodiscard]] bool DapGutterActive() const;

    // Multibuffers follow-up: false when the active buffer carries a
    // MultibufferIndex (Editor/Multibuffer.h) -- unlike every XGutterActive
    // above (which each reserve a column only when there's real data to
    // show), this one hides an otherwise-always-on column: a *vcs diff*-
    // style multibuffer's own composite line numbers (1, 2, 3, ...) are
    // meaningless noise next to the dual old/new line-number columns
    // RequestVcsFullDiffBuffer already bakes directly into the text, so the
    // real gutter would just be redundant clutter, not a useful cross-check.
    [[nodiscard]] bool LineNumberGutterActive() const;

    // Recomputes dapPathKey_ (DapManager::NormalizePathKey of the active
    // buffer's path, empty when pathless) only when the active buffer's
    // identity or its associated path actually changed -- weakly_canonical
    // does real filesystem work, so this must not run per frame. const +
    // mutable members, same shape as EnsureDiagnosticGutterCache.
    void EnsureDapPathKey() const;

    // DAP client slice 3: dap-show-debug's body -- chains stackTrace ->
    // scopes -> variables requests and, once they all land, builds and
    // switches to a read-only "*debug*" buffer: frame lines in the
    // "path:line: text" convention (so C-c C-v visits them for free, same
    // as every results buffer), variable lines carrying a "[ref:N]" marker
    // when composite. dap-expand-variable (ExpandVariableAtPoint) parses
    // that marker back out of point's own line, fetches the children, and
    // splices them in below at deeper indent, consuming the marker so a
    // second expand can't duplicate them.
    void ShowDebugInfo();
    void ExpandVariableAtPoint();
    // ShowDebugInfo's finishing step: joins the accumulated lines into a
    // fresh, read-only "*debug*" buffer (CreateBuffer, uniquified on
    // collision -- same convention BuildResultsBuffer set) and switches to
    // it.
    void BuildDebugBuffer(const std::vector<std::string>& lines);

    // Diagnostic aid, opt-in via $NED_DEBUG_MOUSE (a file path to append
    // to): logs the raw event plus current point/mark/topLine_/size at the
    // top of every mouse handler call, before any of it can be mutated by
    // that call. Added to chase down an intermittent, real-terminal-only
    // (not reproducible headlessly) selection-highlight rendering glitch --
    // see ROADMAP.md. A no-op, effectively free, when the env var is unset.
    void LogMouseEvent(std::string_view event, const MouseEvent& mouse) const;

    // Org-mode fold/unfold follow-up: everywhere in this class that used to
    // reason in raw "buffer line" units now has to skip lines an active
    // Org fold hides (org::FoldedLineRanges) -- these five are the single
    // shared vocabulary for that, used by Paint()'s row loop, CursorPosition(),
    // ScrollToShowPoint(), TopLine()/SetTopLine()/MaxTopLine(), and
    // ByteOffsetForPoint() alike, so none of them can disagree about which
    // lines are actually visible.
    //
    // EnsureHiddenLineRangesCache recomputes hiddenLineRanges_ via
    // org::FoldedLineRanges only when the active buffer pointer, its
    // ContentGeneration(), or its FoldGeneration() have changed since the
    // last call -- and skips calling FoldedLineRanges entirely when
    // buffer.FoldMarkers() is empty, so every buffer that's never had a
    // fold touched (every non-Org buffer, and Org buffers before the first
    // TAB) pays exactly zero extra cost, not just an amortized-cheap one.
    // Marked const/mutable-backed since CursorPosition()/ByteOffsetForPoint()
    // (both const) need a fresh cache just as much as Paint() (non-const)
    // does, and all three can run in either order within a frame.
    void EnsureHiddenLineRangesCache() const;
    // generic-code-folding follow-up: recomputes foldableBlocksCache_ via
    // codefold::FoldableBlocks(mode_, buffer.Text()) only when the active
    // buffer's identity or its ContentGeneration() changed since the last
    // call -- mirrors highlightCacheBuffer_'s own caching shape. Leaves
    // foldableBlocksCache_ empty (and skips calling mode_.fold entirely)
    // whenever mode_.fold itself is empty or editor::CodeFoldingEnabled()
    // is false, which is also exactly the "no gutter affordance" condition
    // Paint()/GutterWidth()/OnMouseEvent all check.
    void EnsureFoldableBlocksCache() const;
    // depth-aware-fold-gutter follow-up: calls EnsureFoldableBlocksCache()
    // first, then (re)derives foldGutterEntries_/foldGutterLineRangesByColumn_
    // from its result -- gated on the buffer plus BOTH ContentGeneration()
    // and FoldGeneration(), since which entries are "expanded" (and so get a
    // line drawn) depends on live FoldMarker state, not just content. See
    // the member declarations' own doc comment for the full history behind
    // exactly what's cached here and why.
    void EnsureFoldGutterCache() const;
    // status-gutter unsaved-change-indicator follow-up: (re)derives
    // unsavedChangeLineRanges_ from buffer.UnsavedChangeRanges() -- gated
    // on the buffer plus BOTH ContentGeneration() and
    // UnsavedChangeGeneration(), since a save clears the ranges (bumping
    // the latter) without necessarily changing content. Called
    // unconditionally every Paint(), unlike EnsureFoldGutterCache -- the
    // status column isn't gated on mode_.fold, every buffer gets one
    // regardless of language.
    void EnsureUnsavedChangeCache() const;
    // LSP client follow-up: (re)derives diagnosticLineSeverities_ from
    // buffer.Diagnostics() -- see that member's own doc comment. Called
    // unconditionally every Paint(), same reasoning as
    // EnsureUnsavedChangeCache: every buffer gets the diagnostic gutter
    // column regardless of language, it's just empty when nothing's been
    // reported.
    void EnsureDiagnosticGutterCache() const;
    // gutter-symbol-kind follow-up: (re)derives symbolGutterLineKinds_ from
    // mode_.symbolKind(buffer.Text()) -- gated on the buffer plus
    // ContentGeneration() alone (no second generation counter needed, unlike
    // EnsureUnsavedChangeCache/EnsureInlineDiagnosticCache -- there's no
    // "cleared independent of content" event for this the way a save clears
    // unsaved ranges or a fresh diagnostics publish replaces diagnostics).
    // Clears (and stamps the cache as up to date, so a repeat call is still
    // a cheap no-op) rather than recomputing at all when mode_.symbolKind is
    // unset or the buffer is read-only -- same eligibility mode_.fold/
    // CodeFoldingEnabled()/ReadOnly() form for FoldGutterActive, just
    // checked inline here instead of a separate public predicate, since
    // SymbolGutterActive() itself is the data-driven "anything to show"
    // question, not this eligibility gate.
    void EnsureSymbolGutterCache() const;
    // VCS blame gutter: unlike EnsureDiagnosticGutterCache/EnsureFoldGutterCache,
    // this does NOT recompute blameLineInfo_ from anything -- there's no
    // cheap synchronous source to recompute it from (populating it means
    // running `git blame`, which is what RequestBlameForCurrentBuffer's
    // async VcsRunner call is for). All this does, called unconditionally
    // every Paint() like the other two: if the active buffer's identity or
    // ContentGeneration() has changed since blameLineInfo_ was last
    // populated, CLEARS it (blame goes stale the instant the buffer is
    // edited or the active buffer changes) rather than trying to
    // resynthesize it -- showing a blank column is the honest answer, not
    // silently-wrong attribution against since-edited line numbers.
    void               EnsureBlameGutterCache() const;
    [[nodiscard]] bool IsLineHidden(std::size_t line) const;
    // `line` if already visible, else the first visible line >= line
    // (capped at limit).
    [[nodiscard]] std::size_t NextVisibleLine(std::size_t line, std::size_t limit) const;
    // Steps forward `count` visible lines from an already-visible `line`,
    // capped at limit.
    [[nodiscard]] std::size_t AdvanceVisibleLines(std::size_t line, std::size_t count, std::size_t limit) const;
    // Count of visible (non-hidden) lines in [startLine, endLineExclusive).
    [[nodiscard]] std::size_t VisibleLineCountBetween(std::size_t startLine, std::size_t endLineExclusive) const;

    // Highlight-overlay predicates used by Paint(); byteOffset is a byte
    // offset into the buffer's current content.
    [[nodiscard]] bool InSelection(std::size_t byteOffset) const;
    // Multi-cursor phase: whether a secondary cursor's point sits exactly
    // at byteOffset -- Paint() inverts that cell as the secondary caret.
    [[nodiscard]] bool IsSecondaryCursorAt(std::size_t byteOffset) const;
    [[nodiscard]] bool InIsearchMatch(std::size_t byteOffset) const;

    // exhaustive-highlighting follow-up: theme_.BrushFor(cls, captureId)
    // through brushCache_ (below) -- the render loop's only path to a
    // syntax brush, so per-capture styling can't accidentally regress the
    // per-codepoint hot path. Flushes the cache itself when
    // editor::SyntaxThemeGeneration() has moved since the last call.
    [[nodiscard]] Brush ResolvedBrush(editor::SyntaxClass cls, editor::CaptureId captureId) const;

    // line-wrap follow-up: editor::EffectiveWrapLines(activeBuffer_.Get().Path(), mode_) --
    // the active buffer's own resolved wrap setting (a per-file override if
    // one is configured, else mode_'s own default). Recomputed fresh each
    // call rather than cached -- it's a cheap map lookup plus a bool copy,
    // the same "not worth caching" judgment CodeFoldingEnabled() callers
    // already make.
    [[nodiscard]] bool EffectiveWrapLines() const;

    ActiveBuffer&          activeBuffer_;
    text::KillRing&        killRing_;
    editor::RegisterTable& registers_;
    editor::PromptHistory& promptHistory_;
    text::BufferList&      bufferList_;
    editor::Dispatcher&    dispatcher_;
    std::string&           statusMessage_;
    const editor::Mode&    mode_;
    const Theme&           theme_;

    // diagnostics-UX follow-up: exactly what Paint()'s per-frame diagnostic
    // echo last wrote into statusMessage_, so that poll only ever
    // overwrites/clears its own message and never a real command result or
    // prompt -- see the poll's own comment in Paint().
    std::string autoDiagnosticMessage_;

    std::size_t topLine_ = 0; // first visible buffer line (0-indexed)
    // The buffer topLine_ was last validated against -- topLine_ itself is
    // BufferView-level state, not per-buffer, so switching which buffer is
    // active (TabBar's own click handler calls ActiveBuffer::Set() directly,
    // with no relationship to BufferView at all, but every other switch path
    // -- find-file, switch-to-buffer, ProjectSidebar's click-to-open, etc. --
    // is exactly as disconnected from topLine_ in principle) can otherwise
    // leave it pointing at a scroll position that doesn't exist in the newly
    // active buffer at all -- a real reported bug (switching from a long
    // file scrolled well past a short file's own last line rendered nothing
    // but blank rows), not hypothetical. EnsureTopLineValidForActiveBuffer,
    // called first thing in Paint() the same way highlightCacheBuffer_/
    // hiddenLineRangesCacheBuffer_/linkCacheBuffer_ already detect "the
    // active buffer changed since I last looked," re-validates topLine_ via
    // ScrollToShowPoint() whenever this doesn't match the buffer Paint() is
    // about to render -- see EnsureTopLineValidForActiveBuffer's own
    // declaration below, alongside this class's other private methods, for
    // why that's sufficient without also needing to reset topLine_ first.
    // Seeded to the buffer active at construction time (not nullptr) so the
    // very first Paint() call is never itself mistaken for a switch -- see
    // the constructor's own comment for the real regression that caught.
    text::Buffer* topLineValidatedBuffer_ = nullptr;

    // line-wrap follow-up: horizontal counterpart to topLine_, only ever
    // meaningful while the active buffer's EffectiveWrapLines() is false --
    // left untouched (not reset) while wrap is active, the same "leave
    // stale scroll state alone rather than force-reset it" precedent
    // topLine_ itself already establishes across a buffer switch.
    std::size_t leftColumn_ = 0;

    // per-buffer-mode follow-up: mirrors topLineValidatedBuffer_'s own
    // "seed at construction so the first Paint() is never mistaken for a
    // switch" precedent exactly -- avoids firing onActiveBufferChanged_
    // (and thus one redundant Mode rebuild) at startup, when the owning
    // Pane already constructed its Mode correctly for this same buffer.
    text::Buffer* modeSyncBuffer_ = nullptr;

    // initial-buffer-diff fix: which buffer the diff gutter was last
    // requested for -- deliberately NOT seeded at construction, unlike
    // modeSyncBuffer_ just above, and tracked separately from it for
    // exactly that reason: while the diff request shared modeSyncBuffer_'s
    // branch, its constructor seeding silently suppressed the initial
    // buffer's diff request too (the file ned was launched on never got
    // markers until an edit/save fired one). See Paint()'s own comment at
    // the use site.
    text::Buffer* diffSyncBuffer_ = nullptr;

    std::size_t                dragAnchor_ = 0;                // point position at the last mouse press, for drag-selection
    std::optional<std::string> debugMouseLogPath_;             // see LogMouseEvent
    ScrollBar*                 scrollBar_           = nullptr; // see SetScrollBar
    ScrollArrowButton*         scrollUpArrow_       = nullptr; // see SetScrollArrows
    ScrollArrowButton*         scrollDownArrow_     = nullptr;
    ProjectSidebar*            projectSidebar_      = nullptr; // see SetProjectSidebar
    Minimap*                   minimap_             = nullptr; // see SetMinimap
    Widget*                    minimapScrollColumn_ = nullptr; // see SetMinimap
    editor::lsp::LspManager*   lspManager_          = nullptr; // see SetLspManager
    editor::tasks::TaskRunner* taskRunner_          = nullptr; // see SetTaskRunner
    editor::vcs::VcsRunner*    vcsRunner_           = nullptr; // see SetVcsRunner
    editor::dap::DapManager*   dapManager_          = nullptr; // see SetDapManager
    editor::acp::AcpManager*   acpManager_          = nullptr; // see SetAcpManager

    // ACP client slice 2: valid only while inputMode_ ==
    // InputMode::AcpPermissionPrompt (populated by ShowAcpPermissionPrompt,
    // consumed by RefreshAcpPermissionPromptStatus/HandleAcpPermissionPromptKey)
    // -- same "not cleared eagerly outside that mode" convention
    // pendingCodeActions_/codeActionSelection_ establish.
    std::vector<editor::acp::AcpManager::PermissionOption> pendingAcpPermissionOptions_;
    std::size_t                                            acpPermissionSelection_ = 0;
    std::string                                            acpPermissionDescription_;

    // EnsureDapPathKey's cache (slice 2): the active buffer's normalized
    // breakpoint-path key, recomputed only when the buffer or its path
    // changes. Mutable for the same const-query reasons every other
    // Ensure*Cache here is.
    mutable const text::Buffer*                  dapPathKeyBuffer_ = nullptr;
    mutable std::optional<std::filesystem::path> dapPathKeyRawPath_;
    mutable std::string                          dapPathKey_;
    EventLoop*                                   eventLoop_ = nullptr; // see SetEventLoop

    InputMode                                inputMode_ = InputMode::Normal;
    std::optional<editor::IncrementalSearch> search_;
    // prefix-argument follow-up: prefixArgReader_ is scoped to a single
    // InputMode::PrefixArgument reading session (emplaced on entry, reset on
    // a terminating key). pendingPrefixArg_ survives past that -- it's what
    // DispatchChordNormally hands the *next* real dispatch as
    // context.prefixArg, pre-clearing the member before that call (see its
    // own doc comment for why: an Invoked outcome can synchronously destroy
    // this BufferView, so nothing after may safely write a member) and
    // restoring it only across a Pending (multi-chord sequence) outcome.
    // Not cancelled by every other interactive-session detour a live
    // prefixArg might precede (e.g. C-u immediately followed by a key that
    // opens ConfirmOverwriteSave) -- a narrow, documented v1 edge case, not
    // exhaustively handled.
    std::optional<editor::PrefixArgumentReader> prefixArgReader_;
    std::optional<long>                         pendingPrefixArg_;
    // The most recent non-empty isearch query, kept across sessions
    // (Accept and Cancel both record it, matching real Emacs' search ring
    // remembering a search string regardless of how the session ended).
    // Ghosted (dimmed) into the echo area while the current session's own
    // query is still empty, and C-s/C-r on an empty query reuses it
    // outright instead of repeating/reversing -- both real Emacs isearch
    // behaviors. Empty until the first isearch session in this BufferView's
    // lifetime accepts or cancels with a non-empty query.
    std::string                             lastSearchQuery_;
    std::optional<editor::QueryReplace>     queryReplace_;
    std::optional<editor::MinibufferPrompt> prompt_; // FindFile/SwitchToBuffer/ProjectSearch, distinguished by inputMode_
    // minibuffer-history-recall follow-up: kNoHistoryIndex means "live
    // editing, not browsing history" -- promptHistoryStash_ is the text
    // prompt_ held right before the first M-p of a browsing run, restored by
    // M-n once it walks back past the newest entry. Both reset in
    // EndInteractiveSession() (every prompt session, successful or
    // cancelled, passes through there). See TryNavigatePromptHistory.
    static constexpr std::size_t          kNoHistoryIndex     = std::numeric_limits<std::size_t>::max();
    std::size_t                           promptHistoryIndex_ = kNoHistoryIndex;
    std::string                           promptHistoryStash_;
    std::optional<editor::ProjectReplace> projectReplace_;
    text::Buffer*                         pendingClose_     = nullptr;               // buffer awaiting y/n in ConfirmCloseBuffer
    TaskPromptAction                      taskPromptAction_ = TaskPromptAction::Run; // see InputMode::TaskName

    // open-binary-anyway follow-up: the path awaiting y/n in
    // ConfirmOpenBinary -- a path, not a Buffer* (unlike pendingClose_),
    // since BufferList::OpenOrCreateFile never got far enough to create one
    // when text::BinaryFileError was thrown. Empty means "no confirmation
    // in flight," the same "unset means not applicable" convention
    // deleteTarget_/renameSource_ already use.
    std::filesystem::path pendingBinaryOpenPath_;

    // session-persistence slice 3: the init file awaiting y/n/a in
    // ConfirmTrustProjectInit, plus the one-shot decision callback handed
    // to RequestTrustProjectInit. Same "empty/unset means not in flight"
    // convention as pendingBinaryOpenPath_ just above.
    std::filesystem::path                                                          pendingTrustInitPath_;
    std::function<void(const std::filesystem::path&, editor::ProjectInitDecision)> onTrustDecision_;

    // Emacs-keymap-round-2 follow-up: the kill-append decision zap-to-char's
    // own invocation made (from context.lastCommand, which the character
    // keystroke that actually performs the kill has no access to -- see
    // CommandContext::zapToCharAppend's own doc comment), stashed here by
    // RunCommandAndHandleOutcome right before StartInteractiveSession enters
    // InputMode::ZapToChar, consumed by HandleZapToCharKey.
    bool pendingZapToCharAppend_ = false;

    DeleteFileStage       deleteStage_ = DeleteFileStage::EnteringPath;
    std::filesystem::path deleteTarget_; // path awaiting y/n in DeleteFileStage::Confirming

    RenameFileStage       renameStage_ = RenameFileStage::EnteringSource;
    std::filesystem::path renameSource_; // path entered in RenameFileStage::EnteringSource

    // backup-and-recovery follow-up: the version list captured when the
    // recover-file session opened (stable for the session's short life --
    // the status line's numbering and the final restore must agree on
    // indices, so this is deliberately not re-listed per keystroke), and
    // the 0-based choice awaiting y/n in RecoverFileStage::Confirming.
    RecoverFileStage                   recoverStage_ = RecoverFileStage::PickingVersion;
    std::vector<editor::BackupVersion> recoverVersions_;
    std::size_t                        recoverChoice_ = 0;

    // execute-extended-command follow-up: index into the ranked candidate
    // list FuzzyFilterAndRank produces fresh from prompt_->Text() on every
    // keystroke/render -- the ranked list itself isn't cached as a member,
    // it's cheap to recompute (a few dozen short strings), matching this
    // codebase's established "recompute, don't cache" convention for cheap
    // per-frame/per-keystroke work (e.g. ScrollArrowButton, WindowManager's
    // own tree walks).
    std::size_t executeCommandSelection_ = 0;

    // project-find-file follow-up: mirrors executeCommandSelection_ above,
    // but projectFindFileCandidates_ itself (project-relative file path
    // strings, populated once by StartInteractiveSession's ProjectFindFile
    // case) IS cached as a member, unlike dispatcher_.Registry().Names() --
    // a real recursive directory walk (editor::BuildProjectTree) is too
    // expensive to redo on every keystroke, unlike an in-memory registry
    // lookup. Only the fuzzy filter/rank against this cached list re-runs
    // per keystroke, matching FuzzyMatch.h's own "cheap to recompute, not
    // cheap to re-enumerate" framing.
    std::vector<std::string> projectFindFileCandidates_;
    std::size_t              projectFindFileSelection_ = 0;

    // rich-theme-set follow-up (Phase 1): the select-theme session's own
    // mirror of the projectFindFile pair above, plus the pre-preview Theme
    // snapshot (see HandleSelectThemeKey's doc comment) and the applier
    // callback (see SetThemeApplier's).
    std::vector<std::string>          selectThemeCandidates_;
    std::size_t                       selectThemeSelection_ = 0;
    std::optional<Theme>              themeBeforePreview_;
    std::function<void(const Theme&)> themeApplier_;

    // kmacro-end-or-call-macro follow-up: reentrancy guard for ReplayMacro --
    // a macro can never structurally contain a call to replay itself (see
    // that method's own doc comment for why), but this is kept anyway as a
    // cheap, unconditional backstop rather than resting entirely on that
    // argument.
    bool replayingMacro_ = false;

    // Window-splitting follow-up: see SetOnWindowRequest/SetOnBufferClosed.
    std::function<void(editor::InteractiveRequest)> onWindowRequest_;
    std::function<void(text::Buffer&)>              onBufferClosed_;
    std::function<void()>                           onTerminalToggle_;      // see SetOnTerminalToggle
    std::function<void()>                           onAcpPanelToggle_;      // see SetOnAcpPanelToggle
    std::function<void(text::Buffer&)>              onActiveBufferChanged_; // see SetOnActiveBufferChanged

    // Caches mode_.highlight's result across Paint() calls (tree-sitter
    // foundation follow-up) -- Paint() runs far more often than the buffer's
    // content actually changes (cursor blink, scrolling, mouse move, an
    // unrelated widget repainting), and mode_.highlight can be a real
    // tree-sitter parse + query run, not a free call. Recomputed only when
    // either the active buffer's identity or its Buffer::ContentGeneration()
    // has changed since the last Paint() -- a real, measured fix, not a
    // preemptive one: an earlier version recomputed unconditionally every
    // Paint() call and regressed a large-JSON [Performance] test to ~217ms
    // per call (10.9s for 50 calls), caught before shipping the same way
    // this project's other perf regressions have been.
    text::Buffer*                      highlightCacheBuffer_     = nullptr;
    std::size_t                        highlightCacheGeneration_ = 0;
    std::vector<editor::HighlightSpan> highlightCacheSpans_;
    // exhaustive-highlighting follow-up: a ned/set-capture-class remap
    // changes the SyntaxClass values *baked into* the cached spans above at
    // parse time, not just how a class renders -- so the cache staleness
    // check compares this against editor::CaptureClassGeneration() too, the
    // same "cheap did-it-change counter" shape ContentGeneration() already
    // has.
    std::size_t highlightCacheClassGeneration_ = 0;

    // per-buffer-highlight-cache follow-up: the three fields just above only
    // remember the *most recently painted* buffer -- switching away and
    // back (A -> B -> A) was a guaranteed miss even though nothing about A
    // itself had changed, forcing a full mode_.highlight() re-run (a real
    // tree-sitter query-capture walk, not a free call) purely because some
    // other buffer got painted in between. This persists that same result
    // across a switch, keyed by buffer identity, alongside the modeName
    // that produced it -- checked in addition to content/class generation
    // because a rename (BufferView::SetPath call sites) can change which
    // Mode applies to a still-open buffer whose content hasn't changed at
    // all, and mode_ itself only resyncs on the *next* active-buffer-change
    // (see WindowManager.cpp's own comment on that), so a stale entry here
    // needs its own independent tell. Cleared via ClearBufferCaches, called
    // from WindowManager::ReassignPanesShowing (the shared close funnel
    // every real close already goes through) so a closed Buffer* never
    // lingers as a cache key indefinitely.
    struct HighlightCacheEntry {
        std::size_t                        contentGeneration = 0;
        std::size_t                        classGeneration   = 0;
        std::string                        modeName;
        std::vector<editor::HighlightSpan> spans;
    };
    std::unordered_map<text::Buffer*, HighlightCacheEntry> highlightCacheByBuffer_;

    // exhaustive-highlighting follow-up: Theme::BrushFor(cls, captureId)
    // resolved once per distinct (class, capture) pair per style
    // generation, not once per rendered codepoint -- the capture-aware
    // overload does a name lookup plus several locked map lookups (the
    // dotted-inheritance walk, Editor/SyntaxTheme.h), which is exactly the
    // per-codepoint-cost class VisualColumn/SpansForLine's own [Performance]
    // histories exist to keep out of the render loop. Keyed by
    // cls-in-the-high-bits | captureId; flushed by ResolvedBrush whenever
    // editor::SyntaxThemeGeneration() has moved (any ned/set-syntax-* or
    // ned/set-capture-* call). Bounded by #distinct-pairs-on-screen, tiny in
    // practice.
    mutable std::unordered_map<std::uint32_t, Brush> brushCache_;
    mutable std::size_t                              brushCacheGeneration_ = 0;
    mutable std::string                              brushCacheThemeName_; // select-theme swaps Theme in place -- see ResolvedBrush

    // structural-selection-expansion follow-up: the stack of prior selection
    // ranges expand-selection has grown through, so shrink-selection can walk
    // back down exactly (the tree alone can't say which child was actually
    // selected on the way up when a node has more than one). Session/UI
    // state, not fundamental Buffer state -- deliberately not stored on
    // Buffer itself, since SetPoint (Buffer.cpp) unconditionally resets its
    // own transient run-state (GoalColumn_/CanAmend_) on every call,
    // including the very SetPoint call expand-selection/shrink-selection
    // make to move the selection, which would erase this history the instant
    // it was written. Staleness-checked the same buffer-identity +
    // ContentGeneration() way as highlightCacheBuffer_ above: a stale
    // (switched-buffer or edited-since) history means "start fresh," not
    // "restore a now-wrong byte range." Cleared outright by
    // RunCommandAndHandleOutcome whenever a dispatched command's own
    // interactiveRequest isn't ExpandSelection/ShrinkSelection -- covers
    // ordinary typing/motion, which never touches interactiveRequest at all.
    text::Buffer*                                    expansionHistoryBuffer_     = nullptr;
    std::size_t                                      expansionHistoryGeneration_ = 0;
    std::vector<std::pair<std::size_t, std::size_t>> expansionHistory_;

    // generic-code-folding follow-up: caches mode_.fold's result across
    // Paint() calls, same shape/reasoning as highlightCacheBuffer_ above --
    // consumed both for the gutter's ▸/▾ rendering and (passed into
    // codefold::FoldedLineRanges) for EnsureHiddenLineRangesCache, so
    // mode_.fold is never called more than once per actually-changed
    // Paint() call. Empty whenever mode_.fold itself is empty or
    // editor::CodeFoldingEnabled() is false -- see EnsureFoldableBlocksCache.
    // Mutable for the same const-query-methods reason
    // hiddenLineRangesCacheBuffer_ already is (EnsureHiddenLineRangesCache,
    // a const method, needs a fresh cache too).
    mutable text::Buffer*                                    foldableBlocksCacheBuffer_     = nullptr;
    mutable std::size_t                                      foldableBlocksCacheGeneration_ = 0;
    mutable std::vector<std::pair<std::size_t, std::size_t>> foldableBlocksCache_;
    // per-buffer-highlight-cache follow-up: same persistence-across-a-switch
    // fix as highlightCacheByBuffer_ above, for mode_.fold instead of
    // mode_.highlight -- see that member's own doc comment for the full
    // reasoning (single-slot eviction on switch, modeName's role, and the
    // ClearBufferCaches/close-funnel cleanup).
    struct FoldableBlocksCacheEntry {
        std::size_t                                       contentGeneration = 0;
        std::string                                        modeName;
        std::vector<std::pair<std::size_t, std::size_t>> ranges;
    };
    mutable std::unordered_map<text::Buffer*, FoldableBlocksCacheEntry> foldableBlocksCacheByBuffer_;
    // depth-aware-fold-gutter follow-up: a small, fixed number of gutter
    // columns (not a viewport-dependent width -- an explicit user choice,
    // so the gutter's own size never jumps around while scrolling past a
    // deeply nested region) reserved for tracing a fold region's extent,
    // one column per nesting level, deeper levels sharing the innermost
    // column.
    static constexpr int kMaxFoldDepthColumns = 4;

    // status/line-number-spacing follow-up: the gutter's own left-to-right
    // layout, left to right -- [status][gap][digits][gap][fold]. kStatusWidth
    // is always reserved (every buffer gets a status column regardless of
    // mode/language); kLineNumberGap appears on BOTH sides of the digits
    // (an explicit user request -- "ensure the line number gutter has an
    // empty space on either side"), not just the trailing one this gutter
    // used to have alone.
    static constexpr std::size_t kStatusWidth   = 1;
    static constexpr std::size_t kLineNumberGap = 1;
    // LSP client follow-up: a second, dedicated 1-column gutter slot for a
    // diagnostic severity marker -- deliberately separate from
    // kStatusWidth's own unsaved-change indicator rather than conflated
    // with it (a line having unsaved edits and a line having a diagnostic
    // are different facts; showing both at once needs two columns, not one
    // shared one). Layout, left to right, is now
    // [status][diagnostic][gap][digits][gap][fold].
    static constexpr std::size_t kDiagnosticWidth = 1;
    // VCS blame gutter: the rightmost gutter region, past fold (layout is
    // now [diff][status][diagnostic][gap][digits][gap][fold][blame] --
    // diff-gutter-markers follow-up moved diff to the front, see
    // kDiffWidth below, without otherwise reordering anything) -- an
    // 8-hex-char short commit hash plus one trailing gap column. Full
    // author/date/summary deliberately isn't crammed in here (there's no
    // tooltip concept in a terminal UI, and a wider fixed column would
    // fight narrow-terminal layouts); that's what the vcs-show-blame
    // multibuffer is for.
    static constexpr std::size_t kBlameWidth = 9;
    // Diff gutter markers follow-up: the leftmost gutter region -- a single
    // solid-color swatch per changed line (added/modified), or a thin
    // notch glyph marking a deletion boundary, matching where real editors
    // (VS Code, GitLens, vim-gitgutter) conventionally put their own git
    // change bars: the very first thing you see, closest to the line
    // numbers. Placed first (not appended at the end the way blame/fold
    // were) specifically to match that convention -- worth the extra
    // column-math churn here for something users expect to recognize at a
    // glance.
    static constexpr std::size_t kDiffWidth = 1;
    // DAP client slice 2: the debug-marker column (breakpoint dot ● /
    // execution arrow ▸) -- leftmost of all when active (see
    // DapGutterActive), before even diff, matching where mainstream
    // debugger UIs put breakpoint dots. Layout when every region is active:
    // [dap][diff][status][diagnostic][gap][digits][gap][fold][blame].
    static constexpr std::size_t kDapWidth = 1;
    // gutter-symbol-kind follow-up: a single glyph (function/type/data) on
    // each definition line, placed between the line-number digits and fold
    // (structure-related columns clustered together) -- layout when every
    // region is active: [dap][diff][status][diagnostic][gap][digits][gap]
    // [symbol][fold][blame]. See SymbolGutterActive's own doc comment for
    // why this is reserved only when the current buffer actually has
    // markers, unlike fold's fixed-width-regardless-of-content reservation.
    static constexpr std::size_t kSymbolWidth = 1;

    struct FoldGutterEntry {
        std::size_t headerLine;
        std::size_t closerLine; // inclusive
        std::size_t blockStart; // FoldMarker key
        int         column;     // == depth; blocks at depth >= kMaxFoldDepthColumns get no entry
                                // at all (fold-gutter-depth-cap follow-up -- see
                                // EnsureFoldGutterCache's own comment; they stay foldable via
                                // code-fold-toggle, just undrawn/unclickable in the gutter)
    };

    // Derived from foldableBlocksCache_ whenever it's recomputed, but gated
    // on BOTH ContentGeneration and FoldGeneration (mirroring
    // hiddenLineRangesCacheContentGeneration_/hiddenLineRangesCacheFoldGeneration_'s
    // own dual-generation pattern below) rather than foldableBlocksCache_'s
    // own content-only gate: foldGutterLineRangesByColumn_ depends on which
    // blocks are currently collapsed/expanded (FoldMarker state), which
    // changes independently of buffer content. Built once per actually-
    // stale Paint() call, O(blocks) with one allocation each -- the same
    // "cache the derived structure, don't rebuild it every Paint() call, and
    // don't reach for a per-element-allocating container under ASan"
    // discipline foldableBlocksCache_'s own doc comment already documents
    // finding the hard way for this exact code path.
    mutable text::Buffer*                foldGutterCacheBuffer_            = nullptr;
    mutable std::size_t                  foldGutterCacheContentGeneration_ = 0;
    mutable std::size_t                  foldGutterCacheFoldGeneration_    = 0;
    mutable std::vector<FoldGutterEntry> foldGutterEntries_; // sorted by headerLine (free -- blocks arrive startByte-sorted)
    mutable std::array<std::vector<std::pair<std::size_t, std::size_t>>, kMaxFoldDepthColumns>
        foldGutterLineRangesByColumn_; // EXPANDED entries only, [headerLine+1, closerLine+1) per column

    // status-gutter unsaved-change-indicator follow-up: converts
    // buffer.UnsavedChangeRanges()' byte ranges to merged, sorted
    // [startLine, endLineExclusive) line ranges for the status column's
    // rendering -- gated on BOTH ContentGeneration() and
    // UnsavedChangeGeneration() (mirrors foldGutterCacheBuffer_'s own
    // dual-generation shape just above), since an edit bumps both but a
    // save only bumps the latter (clearing the ranges without otherwise
    // touching content). Unlike the fold-depth columns, these ranges are
    // flat and disjoint by construction (no nesting concept here at all),
    // so rendering only ever needs a binary search against this cache, no
    // streaming stack state.
    mutable text::Buffer*                                    unsavedChangeCacheBuffer_            = nullptr;
    mutable std::size_t                                      unsavedChangeCacheContentGeneration_ = 0;
    mutable std::size_t                                      unsavedChangeCacheGeneration_        = 0;
    mutable std::vector<std::pair<std::size_t, std::size_t>> unsavedChangeLineRanges_;

    // LSP client follow-up: converts buffer.Diagnostics()' byte ranges to
    // (at most) one {line, severity} entry per line -- a diagnostic's own
    // range can span multiple lines/columns, but the gutter only ever shows
    // a marker on the line it *starts* on, the same "one glyph per line, not
    // a highlighted span" convention most editors' diagnostic gutters use.
    // When more than one diagnostic starts on the same line, the most
    // severe one wins (Error > Warning > Information > Hint). Gated on
    // Buffer::DiagnosticsGeneration() alone -- unlike
    // unsavedChangeCacheContentGeneration_, no separate content-generation
    // check is needed, since SetDiagnostics always replaces the set
    // wholesale (see Buffer::Diagnostic's own doc comment) rather than
    // being incrementally relocated across edits the way fold markers are.
    mutable text::Buffer*                                                           diagnosticGutterCacheBuffer_     = nullptr;
    mutable std::size_t                                                             diagnosticGutterCacheGeneration_ = 0;
    mutable std::vector<std::pair<std::size_t, text::Buffer::Diagnostic::Severity>> diagnosticLineSeverities_; // sorted by line

    // gutter-symbol-kind follow-up: at most one {line, SymbolKind} entry per
    // definition line (the LAST marker wins when a line has more than one --
    // markers arrive startByte-sorted from mode_.symbolKind, so a plain
    // overwrite during the by-line collapse already produces that), sorted
    // by line for the per-row lower_bound lookup Paint() does, same shape as
    // diagnosticLineSeverities_ just above.
    mutable text::Buffer*                                          symbolGutterCacheBuffer_            = nullptr;
    mutable std::size_t                                            symbolGutterCacheContentGeneration_ = 0;
    mutable std::vector<std::pair<std::size_t, editor::SymbolKind>> symbolGutterLineKinds_;

    // inline-diagnostics follow-up: see EnsureInlineDiagnosticCache's own
    // doc comment above for the two-generation gate (diagnostics AND
    // content, unlike the gutter cache just above -- annotation rows shift
    // whole-viewport row math, so a stale line mapping is worse than a
    // stale icon).
    struct InlineDiagnostic {
        text::Buffer::Diagnostic::Severity severity;
        std::size_t                        startByte;
        std::size_t                        endByte;
        std::string                        message;
    };
    mutable text::Buffer*                                     inlineDiagnosticCacheBuffer_            = nullptr;
    mutable std::size_t                                       inlineDiagnosticCacheDiagGeneration_    = 0;
    mutable std::size_t                                       inlineDiagnosticCacheContentGeneration_ = 0;
    mutable std::unordered_map<std::size_t, InlineDiagnostic> inlineDiagnosticsByLine_;

    // VCS blame gutter: populated only by RequestBlameForCurrentBuffer's
    // async completion (never recomputed from Paint() -- see
    // EnsureBlameGutterCache's own doc comment for why), sorted by
    // (0-indexed) line. blameGutterCacheBuffer_/blameGutterCacheContentGeneration_
    // record which buffer+generation blameLineInfo_ is valid for, so
    // EnsureBlameGutterCache can tell it's gone stale and clear it.
    mutable text::Buffer*                                                  blameGutterCacheBuffer_            = nullptr;
    mutable std::size_t                                                    blameGutterCacheContentGeneration_ = 0;
    mutable std::vector<std::pair<std::size_t, editor::vcs::VcsBlameLine>> blameLineInfo_;

    // Diff gutter markers follow-up: live-refreshing added/modified/removed
    // line indicators against HEAD. Unlike blameLineInfo_, this is NOT
    // cleared when the buffer's content goes stale -- a live gutter is
    // expected to briefly show slightly-stale markers during its own
    // debounce window (see ScheduleDiffRefresh), the same tolerance any
    // other live-updating editor's git gutter already has; it's simply
    // overwritten wholesale once a fresh VcsRunner::RequestDiff completes.
    // Sorted by (0-indexed) line -- one entry per changed line for
    // Added/Modified; a Removed entry marks the single line immediately
    // after a pure deletion (a boundary, not a covered range) and is
    // rendered as a thin notch rather than a full swatch.
    enum class DiffLineKind { Added,
                              Modified,
                              Removed };
    mutable std::vector<std::pair<std::size_t, DiffLineKind>> diffLineKinds_;
    // Mirrors completionDebounceTimer_'s own "single pending fire,
    // re-arming cancels the previous one" shape (see that member's doc
    // comment) -- re-armed on every content-changing edit
    // (RunCommandAndHandleOutcome), fired after editor::DiffRefreshDebounce() of
    // idle time. A save bypasses this and refreshes immediately instead
    // (see RunCommandAndHandleOutcome's own save-detection check).
    DeadlineTimer diffRefreshTimer_;

    // VCS vocabulary-completion follow-up: the branch names Tab completes
    // against during InputMode::VcsSwitchBranch -- parked here by
    // BeginVcsSwitchBranchPrompt's branch-list callback (the current
    // branch excluded; switching to it would be a no-op), valid only for
    // that prompt session's lifetime.
    std::vector<std::string> vcsBranchCandidates_;

    // Org-mode fold/unfold follow-up: see EnsureHiddenLineRangesCache's own
    // doc comment above. mutable because CursorPosition()/ByteOffsetForPoint()
    // (both const) refresh it too, the same "cache read by const query
    // methods" shape highlightCacheBuffer_ would also need if any const
    // method ever read it (none currently do).
    mutable text::Buffer*                                    hiddenLineRangesCacheBuffer_            = nullptr;
    mutable std::size_t                                      hiddenLineRangesCacheContentGeneration_ = 0;
    mutable std::size_t                                      hiddenLineRangesCacheFoldGeneration_    = 0;
    mutable std::vector<std::pair<std::size_t, std::size_t>> hiddenLineRanges_;

    // line-wrap follow-up: see EnsureRowCountCache/RowsForLine's own doc
    // comments above. rowCountPerLine_[line] holds only the segment
    // *count* (not the segments themselves) for a line ONCE it's actually
    // been asked about -- kRowCountUnknown (a sentinel, never a real
    // segment count) marks a not-yet-computed entry; ComputeWrapSegments
    // itself is only ever called fresh, per line, on that first access
    // (and again for rendering/cursor placement in Paint()/
    // CursorPosition()/ByteOffsetForPoint(), each of which needs the real
    // segment byte ranges anyway, not just a count) -- never eagerly for
    // the whole buffer, per this cache's own perf history.
    static constexpr std::size_t     kRowCountUnknown                = static_cast<std::size_t>(-1);
    mutable text::Buffer*            rowCountCacheBuffer_            = nullptr;
    mutable std::size_t              rowCountCacheContentGeneration_ = 0;
    mutable int                      rowCountCacheContentWidth_      = 0;
    mutable bool                     rowCountCacheWrapEnabled_       = false;
    mutable std::vector<std::size_t> rowCountPerLine_;

    // Links follow-up: caches org::ParseLinks's result across Paint()/
    // CursorPosition()/ByteOffsetForPoint() calls, same shape/reasoning as
    // highlightCacheBuffer_ above. EnsureLinkCache clears linkCache_ and
    // returns immediately whenever mode_.name != "org-mode" -- a single
    // string compare, cheaper even than FoldMarkers().empty()'s check, so
    // every non-Org buffer (the common case across the whole editor) never
    // calls org::ParseLinks at all. Mutable for the same const-query-methods
    // reason hiddenLineRangesCacheBuffer_ already is.
    mutable text::Buffer*                  linkCacheBuffer_     = nullptr;
    mutable std::size_t                    linkCacheGeneration_ = 0;
    mutable std::vector<editor::org::Link> linkCache_;

    void EnsureLinkCache() const;

    // line-wrap follow-up. RowsForLine generalizes the "every visible line
    // is exactly one canvas row" assumption the fold quartet above bakes
    // in -- true for fold (collapse: 0 or 1 rows) but not for wrap
    // (expansion: 1 or more). 0 if line IsLineHidden; else 1 when
    // EffectiveWrapLines() is false; else ComputeWrapSegments(line).size()
    // when true. Backed by rowCountPerLine_, populated LAZILY (one line's
    // word-break scan computed and memoized the first time that specific
    // line is actually asked about, not eagerly for the whole buffer) --
    // a [Performance] test (Tests/PerformanceTest.cpp) caught a real
    // regression from an earlier eager-whole-range version: MaxTopLine()/
    // ScrollToShowPoint() run every Paint() call, and an eager version paid
    // a real per-line word-break scan across the entire buffer up front,
    // multi-second on a large wrapped document. EnsureRowCountCache only
    // resets rowCountPerLine_'s sizing/keys (a cheap sentinel fill) when
    // buffer identity + ContentGeneration() + content width + wrapLines
    // itself change -- unlike hiddenLineRanges_, row counts genuinely
    // depend on the latter two, which fold does not.
    void                      EnsureRowCountCache() const;
    [[nodiscard]] std::size_t RowsForLine(std::size_t line) const;
    // Row-aware sibling of VisibleLineCountBetween -- sums RowsForLine
    // instead of counting 1 per visible line. Still touches every line in
    // the range (each RowsForLine call is now a cheap, memoized lookup
    // after its first access, per the cache's own doc comment above), so
    // this stays fine to call for a genuinely small range; MaxTopLine/
    // ScrollToShowPoint's own "does everything already fit" checks use
    // VisibleRowCountAtLeast below instead, specifically to avoid ever
    // walking (and so ever computing) more of a huge document than the
    // viewport itself could need.
    [[nodiscard]] std::size_t VisibleRowCountBetween(std::size_t startLine, std::size_t endLineExclusive) const;
    // True as soon as the running row total over [startLine, endLineExclusive)
    // reaches limit -- stops walking (and therefore stops triggering any
    // further RowsForLine word-break computation) the instant the answer is
    // known, rather than always summing the whole range the way
    // VisibleRowCountBetween does. This is what keeps a "does the whole
    // document already fit in one viewport" check cheap regardless of
    // document size: a huge wrapped document's answer is always "no,"
    // discovered within the first `limit`-or-so lines, never by touching
    // the rest of the buffer.
    [[nodiscard]] bool VisibleRowCountAtLeast(std::size_t startLine, std::size_t endLineExclusive, std::size_t limit) const;

    // inline-diagnostics follow-up. Jank-compiler-style annotation rows: a
    // line carrying a diagnostic gets one extra virtual row directly below
    // it -- carets under the span plus the (first line of the) message --
    // rendered by Paint(), never buffer content. The row accounting rides
    // RowsForLine (each annotated line simply reports one more row), which
    // is what keeps CursorPosition/ScrollToShowPoint/MaxTopLine/
    // ByteOffsetForPoint all agreeing about it for free -- the exact same
    // seam line-wrap's own multi-row lines already went through; a click on
    // an annotation row falls into ByteOffsetForPoint's existing
    // clamp-to-last-segment behavior and lands on the annotated line
    // itself, no special case needed (verified against that walk, not
    // assumed).
    //
    // EnsureInlineDiagnosticCache (re)derives inlineDiagnosticsByLine_ --
    // at most one entry per line, most severe first, earliest-starting on a
    // tie, message truncated to its first line -- gated on the buffer plus
    // BOTH DiagnosticsGeneration() (new server push) and
    // ContentGeneration() (edits move byte offsets, so the line a stale
    // span maps to can change), mirroring EnsureFoldGutterCache's own
    // two-generation gate. AnnotationRowsForLine is the 0-or-1 count
    // RowsForLine adds on -- always 0 while
    // editor::InlineDiagnosticsEnabled() is off.
    void                      EnsureInlineDiagnosticCache() const;
    [[nodiscard]] std::size_t AnnotationRowsForLine(std::size_t line) const;
    // Paints one annotation row for `line` at screen row `row`: carets
    // under the diagnostic's visual span (skipped when wrap is on -- the
    // annotation sits below the line's LAST wrap row, where first-row
    // column positions would be a lie -- or when the span is scrolled out
    // of view), then the message, both in the severity's own theme color.
    void PaintInlineDiagnosticRow(Canvas& c, int row, std::size_t line, std::size_t gutterWidth);

    // prose-diagnostic-callout follow-up: the prose/spell/grammar checker's
    // own diagnostics (text::Buffer::Diagnostic::Origin::Prose -- harper-ls
    // via Editor/Lsp/ProseChecker.h, see LspManager::kProseLanguageKey) get
    // no code-style underline or PaintInlineDiagnosticRow annotation row
    // (see the origin filters at those two call sites) -- instead, a small
    // rounded callout brace grows in the pane's right margin, spanning
    // exactly the screen rows the diagnostic's own flagged block occupies
    // (padded by one row above/below for its corners), with the message on
    // the block's own middle row. Called once per Paint(), after the main
    // per-row loop finishes, from Paint() itself.
    //
    // rowLine[row]/rowContentEndColumn[row] are Paint()'s own per-row
    // bookkeeping (index 0..c.size().height): rowLine identifies which
    // buffer line (if any) a row is currently showing real content for --
    // the sentinel kNoRowLine marks a row that isn't (an inline-diagnostic
    // annotation row, or blank space past end-of-buffer) -- and
    // rowContentEndColumn is the rightmost column that row's own content
    // (gutter excluded) actually reached, defaulting to "fully blocked"
    // (the row's own width) for any row Paint() didn't explicitly mark safe,
    // so an unrecognized row can never look like free room to draw into.
    //
    // A diagnostic whose block isn't (fully) on screen, or whose brace
    // wouldn't fit horizontally against every row it would need, is
    // silently skipped entirely -- no partial/shrunk fallback -- relying on
    // the gutter's own severity icon (EnsureDiagnosticGutterCache, which
    // doesn't distinguish origin) and the point-line echo-area hint
    // (BufferView.cpp's own once-per-frame diagnostic-echo poll, likewise
    // origin-agnostic) to still carry the signal, matching this feature's
    // own "otherwise the bottom hint is enough" design.
    static constexpr std::size_t kNoRowLine = static_cast<std::size_t>(-1);
    void                         PaintProseDiagnosticCallouts(Canvas& c, const std::vector<std::size_t>& rowLine,
                                                              const std::vector<int>& rowContentEndColumn, std::size_t gutterWidth);

    // hover/completion follow-up. See Command.h's InteractiveRequest::
    // LspComplete doc comment and this class's own OnAnimation for the
    // request/debounce flow; GhostCompletion itself is transient UI state,
    // not modal -- it coexists with ordinary InputMode::Normal editing
    // rather than replacing it (no dedicated InputMode value), the same way
    // e.g. a diagnostic gutter marker does.
    struct GhostCompletion {
        std::size_t                              requestPoint = 0; // buffer.Point() when this was requested/received -- stale if point has since moved
        std::vector<editor::lsp::CompletionItem> items;
        std::size_t                              selectedIndex = 0;
    };
    std::optional<GhostCompletion> ghostCompletion_;

    // Debounce deadline for an automatic completion request -- set by
    // MaybeScheduleAutoCompletion, consumed by OnAnimation, which fires
    // RequestCompletionAtPoint() once steady_clock::now() reaches it and
    // clears it. std::nullopt means no request pending. Overwriting it on
    // every qualifying keystroke (rather than tracking multiple pending
    // deadlines) is what makes this act as a debounce, not a fixed-interval
    // repeat -- more typing keeps pushing the deadline out.
    std::optional<std::chrono::steady_clock::time_point> completionDebounceDeadline_;
    // FTXUI -> Notcurses migration: the actual wakeup mechanism now --
    // MaybeScheduleAutoCompletion arms this (via EventLoop::Post's Arm) for
    // exactly completionDebounceDeadline_'s own remaining delay each time it
    // moves the deadline out, replacing OnAnimation's own per-frame polling
    // loop. completionDebounceDeadline_ itself is kept anyway (rather than
    // dropped) since RequestCompletionAtPoint's own fired callback still
    // wants a captured, precise "what deadline was I even armed for"
    // record for its own logic.
    DeadlineTimer completionDebounceTimer_;

    // Bumped by RequestCompletionAtPoint before every request; a response
    // whose captured generation no longer matches this is stale (a newer
    // request superseded it, or the user kept typing) and is discarded
    // rather than applied -- the async equivalent of the buffer/point
    // re-check RequestCompletionAtPoint's own callback also does.
    std::size_t completionRequestGeneration_ = 0;

    void                      RequestCompletionAtPoint();
    [[nodiscard]] bool        ShouldSuppressAutoCompletion() const;
    void                      MaybeScheduleAutoCompletion(const editor::KeyChord& chord, std::size_t generationBefore);
    void                      AcceptGhostCompletion();
    void                      CycleGhostCompletion(int direction);
    [[nodiscard]] std::string GhostSuffixFor(const editor::lsp::CompletionItem& item) const;

    // code-actions follow-up: pendingCodeActions_/codeActionSelection_ are
    // valid only while inputMode_ is LspCodeActionSelect/LspCodeActionConfirm
    // (see RequestCodeActionsAtPoint's own doc comment above for why
    // inputMode_ only ever changes from inside that async callback).
    // codeActionRequestGeneration_ mirrors completionRequestGeneration_'s
    // exact staleness-guard shape.
    std::vector<editor::lsp::CodeAction> pendingCodeActions_;
    std::size_t                          codeActionSelection_         = 0;
    std::size_t                          codeActionRequestGeneration_ = 0;

    // executeCommand/prose-code-actions follow-up: which LspManager
    // serverKey pendingCodeActions_ was requested from -- empty for the
    // primary language server (RequestCodeActions's own default), else
    // editor::lsp::kProseLanguageKey when point sat on a Prose-origin
    // diagnostic. Threaded through ResolveAndApplyCodeAction/ApplyCodeAction
    // so a command carried by one of these actions executes against the
    // same connection it was listed from.
    std::string codeActionServerKey_;

    // go-to-definition follow-up: same staleness-guard/selection-list shape
    // as pendingCodeActions_/codeActionSelection_/codeActionRequestGeneration_
    // just above, valid only while inputMode_ == LspGotoDefinitionSelect.
    std::vector<editor::lsp::LspManager::ResolvedLocation> pendingDefinitions_;
    std::size_t                                            definitionSelection_         = 0;
    std::size_t                                            definitionRequestGeneration_ = 0;

    // header-source-switching follow-up: same staleness-guard shape as
    // definitionRequestGeneration_ above, no selection list needed --
    // switchSourceHeader never returns more than one candidate.
    std::size_t switchHeaderSourceRequestGeneration_ = 0;

    // rename follow-up: same staleness-guard shape once more.
    // pendingRename_/renameTitle_ are valid only while inputMode_ ==
    // LspRenameConfirm -- renameTitle_ is the human-readable "N edits across
    // M files" summary RefreshRenameConfirmStatus computes once, up front,
    // rather than recomputing it on every keypress at the confirmation
    // (there's nothing to recompute it *for*, unlike RefreshCodeActionSelectStatus's
    // own per-keystroke Up/Down refresh).
    std::optional<editor::lsp::LspManager::ResolvedRename> pendingRename_;
    std::string                                            renameTitle_;
    std::size_t                                            renameRequestGeneration_ = 0;

    // status-message-lifecycle follow-up. A uniform rule for statusMessage_,
    // regardless of who wrote it (any command via CommandContext::message,
    // any Handle*Key prompt/session, or this class's own pending-key-
    // sequence/undefined-key reporting below): it clears itself after a
    // short idle timeout, OR immediately on the next real dispatched
    // command that doesn't itself set a new message -- whichever comes
    // first. Deliberately observational (a per-Paint()-call diff against
    // the last-seen value) rather than hooking every one of the dozens of
    // call sites that write statusMessage_ directly -- see
    // EnsureStatusMessageFreshness's own doc comment for the exact
    // mechanism and its one known trade-off.
    std::string                                          statusMessageSnapshot_;
    std::optional<std::chrono::steady_clock::time_point> statusMessageChangedAt_;
    static constexpr std::chrono::seconds                kStatusMessageTimeout{4};
    // See completionDebounceTimer_'s own comment -- same replacement for
    // OnAnimation's per-frame polling, this time backing
    // statusMessageChangedAt_'s idle-clear deadline.
    DeadlineTimer statusMessageTimer_;

    // Detects a statusMessage_ change since the last call (from Paint(),
    // which runs after every real render including every keystroke) and
    // (re)arms the idle-clear deadline via statusMessageTimer_, which fires
    // it once the deadline passes with nothing further changing it.
    void EnsureStatusMessageFreshness();
};

} // namespace ned::ui

#endif // NED_UI_BUFFERVIEW_H
