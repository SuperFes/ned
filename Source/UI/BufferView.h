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
#include <set>
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
#include "Editor/DiagnosticsLog.h"
#include "Editor/DiffRefreshSettings.h"
#include "Editor/Dispatcher.h"
#include "Editor/EmbeddedDocuments.h"
#include "Editor/ExpandableTree.h"
#include "Editor/IncrementalSearch.h"
#include "Editor/Link.h"
#include "Editor/LinkedEditingSession.h"
#include "Editor/Lsp/LspManager.h"
#include "Editor/MinibufferPrompt.h"
#include "Editor/Mode.h"
#include "Editor/Org.h"
#include "Editor/PointerGraphNode.h"
#include "Editor/PrefixArgument.h"
#include "Editor/ProjectRegistry.h"
#include "Editor/ProjectReplace.h"
#include "Editor/ProjectTrust.h"
#include "Editor/PromptHistory.h"
#include "Editor/QueryReplace.h"
#include "Editor/Register.h"
#include "Editor/Snippet.h"
#include "Editor/Tasks/TaskRunner.h"
#include "Editor/TestRun/TestRunner.h"
#include "Editor/Vcs/VcsProvider.h"
#include "Editor/Vcs/VcsRunner.h"
#include "Editor/Vim/VimEngine.h"
#include "EventLoop.h"
#include "ListPopup.h"
#include "Minimap.h"
#include "ProjectSidebar.h"
#include "ScrollArrowButton.h"
#include "ScrollBar.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "Text/KillRing.h"
#include "Theme.h"
#include "TreeView.h"
#include "VcsPanel.h"
#include "WhichKeyHint.h"

namespace ned::janet {
class Environment; // Self-hosting-completion follow-up: see SetJanetEnvironment below -- only ever held as a raw pointer here, so a forward declaration is enough.
} // namespace ned::janet

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

    // jump-back-stack follow-up: public so a test can assert the eviction
    // cap without reaching into jumpBackStack_ itself -- same "expose a
    // small, honest introspection point" precedent as MinimapActive()/
    // ScrollColumnActive() below.
    static constexpr std::size_t kMaxJumpBackStack = 64;

    void Paint(Canvas c) override;
    bool OnEvent(const Event& event) override;
    bool Focusable() const override;

    // Local cursor position for the real terminal caret. A pure, independent
    // computation, deliberately NOT cached from Paint() -- see the .cpp
    // definition's own comment for why that caused a real, reported
    // one-frame-stale cursor bug.
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
    // (the default) means the toggle command is a no-op. Flipping .active
    // alone is sufficient -- every widget recomputes its own layout/paint
    // fresh each frame, so no separate forced-reflow step is needed.
    void SetProjectSidebar(ProjectSidebar* sidebar);

    // VCS side panel: registers the panel so ToggleVcsPanel/FocusVcsPanel
    // (toggle-vcs-panel/focus-vcs-panel) can drive it, and so both requests
    // -- and ToggleProjectSidebar/FocusProjectSidebar above -- can keep
    // this panel and projectSidebar_ mutually exclusive on the shared left
    // dock slot: expanding one collapses the other first (via plain
    // SetCollapsed, not the committing CommitCollapsed/ToggleCollapsed --
    // an automatic side effect of the *other* widget's toggle shouldn't
    // overwrite that other widget's own persisted visibility preference).
    // nullptr (the default) means no-op, same "unset is a safe no-op"
    // convention SetProjectSidebar establishes; a project with no VCS
    // provider configured just never gets this wired in main.cpp.
    void SetVcsPanel(VcsPanel* panel);

    // VCS side panel: starts an existing VCS interactive flow (commit
    // compose / branch switch / branch create) on this pane -- the same
    // entry points InteractiveRequest::VcsCommit/VcsSwitchBranch/
    // VcsCreateBranch already drive from C-c v c/w/n, given a second
    // trigger here since VcsPanel has no BufferView& of its own.
    // WindowManager::RequestVcsPanelAction (routed to whichever pane has
    // focus, RequestOpenBinaryFile's own shape) is the real caller.
    void RequestVcsAction(VcsPanelAction action);

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

    // embedded-language-documents follow-up: the embedded language governing
    // point right now (e.g. "javascript" while point sits inside an HTML
    // <script> block), or nullopt for the ordinary case -- point outside any
    // embedded region, or mode_.embeddedRegions unset entirely. Reuses this
    // frame's already-cached embedded-document set (EnsureEmbeddedDocumentCache),
    // recomputing it first if this is called before the current Paint() has.
    // Shared by ModeLine's "language at point" display (via
    // SetLanguageAtPointProvider) and, at an arbitrary offset rather than
    // point, LSP request routing below.
    [[nodiscard]] std::optional<std::string> EmbeddedLanguageAtPoint();

    // embedded-language-documents follow-up: EmbeddedLanguageAtPoint's
    // sibling for an arbitrary byte offset rather than point -- what
    // RequestCompletionAtPoint/RequestDefinitionAtPoint/RequestRenameAtPoint
    // resolve before calling into LspManager, so a request issued inside an
    // embedded region routes to that language's own server instead of the
    // host's. Returns "" (meaning "use the primary/host server," matching
    // LspManager::ResolveSyncState's own empty-serverKey convention) when
    // byteOffset isn't inside any embedded region.
    [[nodiscard]] std::string ResolvedLspServerKey(std::size_t byteOffset);

    // task-runner follow-up: registers the shared TaskRunner, forwarded to
    // CommandContext::taskRunner before each dispatch so run-task/
    // cancel-task can reach it -- same "unset is a safe no-op" convention
    // SetLspManager already establishes.
    void SetTaskRunner(editor::tasks::TaskRunner* taskRunner);

    // project-undo follow-up: registers the shared ProjectUndoManager,
    // forwarded to CommandContext::projectUndo before each dispatch so
    // undo/redo can fold a multi-file LSP edit's sibling files into a
    // single invocation -- same "unset is a safe no-op" convention
    // SetLspManager already establishes (plain per-buffer undo/redo keeps
    // working unchanged if this was never called).
    void SetProjectUndo(editor::ProjectUndoManager* projectUndo);

    // test-runner integration: registers the shared TestRunner -- same
    // "unset is a safe no-op" convention as SetTaskRunner (the run-tests
    // family reports "No test runner available." via statusMessage_ if this
    // was never called).
    void SetTestRunner(editor::testrun::TestRunner* testRunner);

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

    // user-facing-hang-affordance follow-up (ChildProcess-hang-protection-
    // round-2 -- see ROADMAP.md). Whether this pane's Paint() polls
    // editor::HasUnseenDiagnosticsLogEntry() and surfaces a live
    // "New warning -- see *Messages*" statusMessage_ the first time it sees
    // one -- default false, same "unset is a safe no-op" convention as every
    // other Set* hook here. Deliberately opt-in rather than always-on: that
    // flag is process-wide state (Editor/DiagnosticsLog.h), not a per-pane
    // resource the way LspManager/TaskRunner/etc. are, so a bare
    // test-constructed BufferView must not read it by default -- doing so
    // unconditionally let an unrelated test's own LogMessage call leak into
    // any other test's Paint()-time statusMessage_ assertion, confirmed live
    // via a real intermittent Catch2 --order rand failure before this guard
    // existed. WindowManager::Pane's constructor is the one real caller,
    // enabling it for every actually-composed editor pane.
    void SetSurfaceUnseenLogEntries(bool enabled);

    // Self-hosting-completion follow-up: registers the process-wide
    // janet::Environment so ghost-text completion in a Janet-mode buffer can
    // fuzzy-complete against every live "ned/*" binding name -- same "unset
    // is a safe no-op" convention as SetLspManager (no completion source
    // besides plain dabbrev-expand if this was never called). Queried fresh
    // on every completion request rather than snapshotted, so it reflects
    // whatever's actually bound right now (see Environment::
    // BindingNamesWithPrefix's own doc comment).
    void SetJanetEnvironment(const janet::Environment* janetEnv);

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
    // Hunk-navigation follow-up: same seam again, for JumpToNextHunk/
    // JumpToPreviousHunk -- both are fully synchronous (a plain search over
    // diffHunkStartLines_, no VcsRunner round trip), so these wrappers need
    // no live EventLoop at all, unlike most *ForTesting entries above.
    void JumpToNextHunkForTesting();
    void JumpToPreviousHunkForTesting();
    // next-error follow-up: same seam again, for NextError/PreviousError --
    // fully synchronous (Editor/NextError.h's CollectResultLocations is a
    // plain scan, no VcsRunner/LSP round trip), so no live EventLoop needed.
    void NextErrorForTesting();
    void PreviousErrorForTesting();

    // Diagnostics-multibuffer follow-up: same "public primarily for tests"
    // seam, but RequestDiagnosticsBuffer needs no live EventLoop at all --
    // it's fully synchronous, no VcsRunner-style callback in between. This
    // is a plain passthrough rather than a partial "guards only" exposure.
    void RequestDiagnosticsBufferForTesting();

    // find-all-references follow-up: same seam again, for
    // RequestProjectFindReferences -- fully synchronous, no live EventLoop
    // needed, same reasoning as RequestDiagnosticsBufferForTesting above.
    void RequestProjectFindReferencesForTesting();

    // call/type-hierarchy follow-up. Which of the four requests a session
    // browses -- fixed for the session's whole lifetime (chosen once, at
    // RequestHierarchyAtPoint's own entry point), since every further
    // expand in the tree re-asks the *same* direction against whichever
    // node the user opens next (a caller browsing "who calls this" expects
    // every subsequent expand to keep answering "who calls *that*", not
    // switch direction mid-tree). Public (unlike every other Lsp* session's
    // own enum/state in this class) purely so RequestHierarchyAtPointForTesting
    // below can take one -- the InteractiveRequest dispatch path
    // (StartInteractiveSession's own switch) is what a real keybinding
    // actually goes through, same as RequestProjectFindReferencesForTesting's
    // own reasoning.
    enum class HierarchyDirection { IncomingCalls,
                                    OutgoingCalls,
                                    Supertypes,
                                    Subtypes };

    // Same "public primarily for tests" seam as RequestProjectFindReferencesForTesting
    // above -- RequestHierarchyAtPoint itself is private since a real
    // keybinding reaches it only via StartInteractiveSession's own switch.
    void RequestHierarchyAtPointForTesting(HierarchyDirection direction);

    // Debugging wishlist follow-up: same "public primarily for tests" seam
    // as RequestHierarchyAtPointForTesting above -- RequestPointerGraphAtPoint
    // itself is private since a real keybinding reaches it only via
    // StartInteractiveSession's own switch (dap-show-pointer-graph, M-x only).
    void RequestPointerGraphAtPointForTesting();

    // Registers the EventLoop used to end the whole app on `quit`/confirmed
    // ConfirmQuit, and to back completionDebounceDeadline_/
    // statusMessageChangedAt_'s own DeadlineTimer-based deadlines (see their
    // doc comments below). Unset (the default, nullptr) makes `quit` a
    // no-op instead of a null-deref -- every unit test, and any other
    // headless use of BufferView, relies on this.
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

    // edit-application-gaps follow-up: entry point for a server-pushed
    // workspace/applyEdit request (LspManager::SetApplyEditHandler,
    // WindowManager::ApplyServerPushedWorkspaceEdit) -- unlike ApplyRename/
    // ApplyCodeAction, there's no particular buffer/point this originates
    // from, so it's routed to whichever pane has focus, the same "no single
    // owner, route to focus" shape RequestOpenBinaryFile/
    // RequestTrustProjectInit already establish (see WindowManager.cpp).
    // Shares ApplyRename's own all-or-nothing-open/refuse-on-
    // touchesUnsupportedForm contract (both fold into the same private
    // ApplyResolvedWorkspaceEdit helper) and returns whether it actually
    // applied, which the caller reports back to the server as the spec's
    // own {applied: bool} response.
    [[nodiscard]] bool ApplyServerPushedWorkspaceEdit(const editor::lsp::LspManager::ResolvedRename& edit, const std::string& label);

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

    // Split-resize follow-up: the same "checked first, regardless of
    // position, ahead of this widget's own mouse handling" cooperation
    // OnMouseEvent already gives ProjectSidebar's own IsResizing() -- but a
    // query callback rather than a single owned pointer, since WindowManager
    // may own any number of live split dividers (nested splits) at once, not
    // just one. Unset (or a null/never-true query) is a safe no-op, matching
    // every other Set* hook here.
    void SetSplitResizeQuery(std::function<bool()> query);

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

    // ACP checkpoint/rewind follow-up: acp-rewind's forwarding hook, same
    // shape as SetOnAcpPanelToggle immediately above -- the picker itself
    // lives in AcpPanel (another OverlayHost overlay owned by main.cpp's
    // composition), wired via WindowManager::SetOnAcpRewindRequest fanning
    // out to every pane. Unset is a safe no-op (reported via statusMessage_
    // instead, see the InteractiveRequest::AcpRewind case).
    void SetOnAcpRewindRequest(std::function<void()> handler);

    // DAP round 2: dap-toggle-console's forwarding hook, same shape and
    // reasoning as SetOnAcpPanelToggle immediately above -- the debug
    // console is another OverlayHost overlay owned by main.cpp's
    // composition, wired via WindowManager::SetOnDapConsoleToggle fanning
    // out to every pane. Unset is a safe no-op.
    void SetOnDapConsoleToggle(std::function<void()> handler);

    // generic-popup follow-up: list-buffers' forwarding hook, same shape and
    // reasoning as SetOnDapConsoleToggle immediately above -- the buffer-list
    // panel is another OverlayHost overlay owned by main.cpp's composition,
    // wired via WindowManager::SetOnBufferListToggle fanning out to every
    // pane. Unset is a safe no-op.
    void SetOnBufferListToggle(std::function<void()> handler);

    // which-key follow-up: same OverlayHost-owned-above-this-class shape as
    // SetOnTerminalToggle/SetOnAcpPanelToggle/SetOnDapConsoleToggle, but
    // fired on every Pending/non-Pending transition rather than by an
    // explicit toggle command -- called with a populated WhichKeyHint the
    // instant a prefix chord (C-x, C-c, ...) becomes pending, and with
    // std::nullopt the instant it resolves (Invoked) or fails (Unbound).
    // Wired via WindowManager::SetOnPrefixHintChanged fanning out to every
    // pane; main.cpp's registrant shows/hides a shared ListPopup overlay
    // (generic-popup follow-up; was a dedicated WhichKeyPopup). Unset is a
    // safe no-op.
    void SetOnPrefixHintChanged(std::function<void(std::optional<WhichKeyHint>)> handler);

    // generic-popup follow-up (Phase 3): same OverlayHost-owned-above-this-
    // class shape as SetOnPrefixHintChanged immediately above, for the
    // candidate lists behind M-x/project-find-file/find-recent-file/
    // bookmark-jump/select-theme/LSP code-action-select -- called with a
    // populated ListPopupModel every time one of those sessions' own
    // Refresh*Status method runs, and with std::nullopt whenever
    // EndInteractiveSession clears any session (this fires unconditionally
    // there, whether or not the just-ended session ever used it -- a
    // std::nullopt callback for an already-hidden popup is a no-op, the
    // same tolerance SetOnPrefixHintChanged's own std::nullopt case has).
    // statusMessage_ itself only ever carries the short prompt text now
    // (e.g. "M-x ") for these sessions -- the candidate list lives in the
    // popup instead of being squeezed into that one row. Wired via
    // WindowManager::SetOnCandidatesChanged fanning out to every pane;
    // main.cpp's registrant shows/hides a shared ListPopup overlay. Unset
    // is a safe no-op.
    void SetOnCandidatesChanged(std::function<void(std::optional<ListPopupModel>)> handler);

    // completion-popup follow-up: same OverlayHost-owned-above-this-class
    // shape as SetOnCandidatesChanged immediately above, but for a
    // structurally different session -- ActiveCompletion (renamed from
    // GhostCompletion) is transient, non-modal UI that coexists with live
    // InputMode::Normal typing, unlike the modal EchoArea-prompt-driven
    // sessions SetOnCandidatesChanged serves, so it gets its own hook/popup
    // instance rather than sharing that one. Fired with a populated
    // ListPopupModel (its `anchor` field set to point's current on-screen
    // position) whenever activeCompletion_ changes, and with std::nullopt
    // when it's cleared or point's row scrolls off screen. Wired via
    // WindowManager::SetOnCompletionChanged fanning out to every pane;
    // main.cpp's registrant shows/hides its own shared, anchor-aware
    // ListPopup overlay. Unset is a safe no-op.
    void SetOnCompletionChanged(std::function<void(std::optional<ListPopupModel>)> handler);

    // mouse-support follow-up: the click-driven counterpart to
    // AcceptActiveCompletion() -- accepts whichever row was clicked rather
    // than whatever's currently selected (a click and the current selection
    // needn't agree; a click is itself a selection). A no-op if
    // activeCompletion_ is unset or index is out of range (e.g. a stale
    // click racing a just-cleared popup). Public (unlike
    // AcceptActiveCompletion() and activeCompletion_ itself) because the
    // click arrives from outside this class -- WindowManager::
    // ActivateCompletionAt forwards here from the completion popup's own
    // ListPopup::SetOnActivate in main.cpp.
    void AcceptActiveCompletionAt(std::size_t index);

    // call/type-hierarchy follow-up: same OverlayHost-owned-above-this-
    // class shape as SetOnCandidatesChanged/SetOnCompletionChanged above,
    // for the one shared ui::TreeView overlay every hierarchy-browse
    // session (lsp-call-hierarchy-incoming/-outgoing, lsp-type-hierarchy-
    // supertypes/-subtypes) uses. Fired with a populated TreeViewModel
    // every time PushHierarchyModel runs, and with std::nullopt when
    // EndHierarchySession clears the session. Wired via
    // WindowManager::SetOnHierarchyChanged fanning out to every pane (see
    // that method's own doc comment for why the fan-out there is not a
    // plain forward the way SetOnCandidatesChanged's is); main.cpp's
    // registrant shows/hides a shared TreeView overlay and hands it
    // keyboard focus. Unset is a safe no-op.
    void SetOnHierarchyChanged(std::function<void(std::optional<ui::TreeViewModel>)> handler);

    // The five methods a hierarchy session's TreeView overlay drives, once
    // it holds keyboard focus (unlike every ListPopup-backed session above,
    // which never leaves BufferView's own focus at all -- see
    // HierarchySession's own doc comment for why this one genuinely needs
    // a different wiring shape). Each is a no-op without an active
    // hierarchySession_, or when index is out of range -- a stale callback
    // racing an already-ended session is handled the same tolerant way
    // AcceptActiveCompletionAt handles a stale click. Public, and reached
    // via WindowManager::HierarchyActivate/HierarchyToggleExpand/
    // HierarchyCollapse/HierarchyCancel/HierarchySelectionChanged, which
    // route to whichever pane's BufferView currently owns the visible
    // session (WindowManager remembers this explicitly -- FocusedPane()
    // itself can't, since this BufferView is no longer Focused() once the
    // TreeView overlay holds the keyboard).
    void HierarchyActivate(std::size_t index);
    void HierarchyToggleExpand(std::size_t index);
    void HierarchyCollapse(std::size_t index);
    void HierarchyCancel();
    void HierarchySelectionChanged(std::size_t index);

    // Debugging wishlist follow-up (pointer/linked-list graph view):
    // SetOnHierarchyChanged's own mirror, for the pointer-graph session's own
    // shared TreeView overlay (a second, independent overlay from the
    // hierarchy browser's -- see main.cpp's own wiring). Fired with a
    // populated TreeViewModel every time PushPointerGraphModel runs, and with
    // std::nullopt when EndPointerGraphSession clears the session. Wired via
    // WindowManager::SetOnPointerGraphChanged. Unset is a safe no-op.
    void SetOnPointerGraphChanged(std::function<void(std::optional<ui::TreeViewModel>)> handler);

    // The five methods a pointer-graph session's TreeView overlay drives --
    // Hierarchy{Activate,ToggleExpand,Collapse,Cancel,SelectionChanged}'s own
    // mirror, same no-op-when-no-session/out-of-range tolerance. Reached via
    // WindowManager::PointerGraph{Activate,ToggleExpand,Collapse,Cancel,
    // SelectionChanged}, which route to whichever pane's BufferView currently
    // owns the visible session the same way the hierarchy routers do. Per
    // this feature's own v1 scope cut, Activate just toggles expand/collapse
    // (like ToggleExpand) -- there's no natural "jump to source" target for a
    // plain runtime variable the way there is for a hierarchy item's
    // definition site.
    void PointerGraphActivate(std::size_t index);
    void PointerGraphToggleExpand(std::size_t index);
    void PointerGraphCollapse(std::size_t index);
    void PointerGraphCancel();
    void PointerGraphSelectionChanged(std::size_t index);

    // named-projects follow-up: AcceptActiveCompletionAt's own "public
    // because the trigger arrives from outside this class" shape --
    // WindowManager::TriggerSwitchProject forwards here from
    // ProjectSidebar's header-row click (main.cpp wiring), the same way a
    // keybinding reaches StartInteractiveSession via the ordinary
    // command-dispatch path.
    void TriggerSwitchProject();

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
                           // named-projects follow-up: ProjectFindFile's own fuzzy-narrowed
                           // picker shape, over registered project names/roots instead of
                           // files -- see HandleSwitchProjectKey/RefreshSwitchProjectStatus.
                           SwitchProject,
                           // named-projects follow-up: a plain path-entry prompt, FindFile's
                           // own shape including its tab-completion (see CompletePrompt) --
                           // routed through the shared HandlePromptKey else-chain like
                           // FindFile/BookmarkSetName, not a dedicated handler. Enter checks
                           // whether the resolved root is already registered (activate
                           // directly) or transitions to OpenProjectName (BookmarkSetName's
                           // own pre-filled-second-prompt shape) for a not-yet-known one.
                           OpenProjectPath,
                           // named-projects follow-up: BookmarkSetName's own shape --
                           // pre-filled with the directory's basename, editable, Enter
                           // registers pendingOpenProjectRoot_ under the typed name and
                           // activates it.
                           OpenProjectName,
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
                           // property-drawers follow-up: org-set-property's own
                           // two-stage session (property name, then its value) --
                           // dedicated HandleSetPropertyKey, RenameFileStage's exact
                           // shape (see propertyStage_), not routed through the
                           // shared HandlePromptKey else-chain SetHeadlineTags uses.
                           // org-delete-property is a single prompt and DOES go
                           // through that shared chain.
                           SetProperty,
                           DeleteProperty,
                           // scheduling/recurrence follow-up: org-schedule/org-deadline --
                           // single prompt each, same shared HandlePromptKey else-chain
                           // SetHeadlineTags/DeleteProperty already use, pre-filled with
                           // the headline's current SCHEDULED:/DEADLINE: timestamp if it
                           // has one (see StartInteractiveSession's own cases).
                           OrgSchedule,
                           OrgDeadline,
                           // org-capture follow-up: single-stage, same "read exactly
                           // one further character, no MinibufferPrompt" shape as
                           // PointToRegister/etc. -- see HandleOrgCaptureKey.
                           OrgCaptureSelectTemplate,
                           // code-actions follow-up: entered only once RequestCodeActionsAtPoint's
                           // async response actually arrives (never eagerly, while the request is
                           // still in flight -- see that method's own doc comment), and only when
                           // more than one action came back -- a single action, or one picked from
                           // this list, applies directly with no separate y/n confirmation.
                           LspCodeActionSelect,
                           // go-to-definition follow-up: same "entered only from inside the
                           // async response callback" shape as LspCodeActionSelect above --
                           // Select when RequestDefinitionAtPoint's response names more than one
                           // location (a real, if less common, case -- e.g. a virtual/overridden
                           // method with several implementations).
                           LspGotoDefinitionSelect,
                           // symbol-search follow-up: LspGotoSymbol is entered only once
                           // RequestDocumentSymbolsAtPoint's async response arrives (same
                           // "entered from inside the callback" shape as LspGotoDefinitionSelect
                           // above), but the session itself is ProjectFindFile's fuzzy-narrowed
                           // picker shape, not a numbered list -- a buffer's symbol count can be
                           // large. LspWorkspaceSymbol opens immediately (no request to wait
                           // for up front, an empty query is itself the first request) and stays
                           // async for its whole session: every keystroke re-sends
                           // workspace/symbol (debounced), replacing the candidate list each
                           // time, rather than fuzzy-filtering one already-fetched list locally.
                           LspGotoSymbol,
                           LspWorkspaceSymbol,
                           // rename follow-up: LspRenameNewName is the one synchronous
                           // prompt-shaped stage here (routed through HandlePromptKey, like
                           // FindFile/CreateDirectory/etc.) -- Enter sends the actual
                           // textDocument/rename request via RequestRenameAtPoint, which
                           // applies the result directly once the response arrives (from
                           // inside its own async callback), no separate y/n confirmation.
                           LspRenameNewName,
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
                           PrefixArgument,
                           // snippet-expansion follow-up: a live tabstop
                           // session (Editor/Snippet.h's SnippetSession) --
                           // TAB/S-TAB hop fields, ESC ends, and everything
                           // else re-dispatches through DispatchChordNormally
                           // (HandlePrefixArgumentKey's own re-dispatch
                           // shape), with RunCommandAndHandleOutcome's
                           // snippet hooks wrapping each dispatched edit and
                           // its mirror sync in one undo group. Being a
                           // non-Normal mode, macro replay stops at an
                           // expansion (ReplayMacro's existing rule) -- an
                           // inherited, deliberate limitation.
                           Snippet,
                           // DAP round 2: DapBreakpointCondition/DapBreakpointLogMessage/
                           // DapAddWatch/DapSetVariableValue are HandlePromptKey-routed
                           // plain-text prompts, same shape as DapEvaluate above.
                           // DapThreadSelect is different in kind, same way
                           // AcpPermissionPrompt is -- entered directly from
                           // RequestThreads' async callback, driven by its own
                           // HandleDapThreadSelectKey (LspCodeActionSelect's numbered-list
                           // shape).
                           DapBreakpointCondition,
                           DapBreakpointLogMessage,
                           DapAddWatch,
                           DapSetVariableValue,
                           DapThreadSelect,
                           // DAP round 3: DapBreakpointHitCondition/DapFunctionBreakpointName
                           // are two more HandlePromptKey-routed plain-text prompts, the
                           // condition/logMessage/AddWatch shape above.
                           // DapExceptionFilterSelect is different in kind, DapThreadSelect's
                           // own shape -- own dedicated handler
                           // (BeginDapExceptionFilterSelect/RefreshDapExceptionFilterStatus/
                           // HandleDapExceptionFilterSelectKey), except multi-select/toggle
                           // instead of single-pick.
                           DapBreakpointHitCondition,
                           DapFunctionBreakpointName,
                           DapExceptionFilterSelect,
                           // DAP round 5: DapMemoryByteCount is one more
                           // HandlePromptKey-routed plain-text prompt
                           // (dap-show-memory-at-point, ShowMemoryAtPoint's
                           // second half) -- empty/unparsable input defaults
                           // to a fixed byte count rather than failing,
                           // unlike the breakpoint-field prompts above where
                           // empty is itself meaningful (clears the field).
                           DapMemoryByteCount,
                           // editor-ergonomics follow-up: FindRecentFile is
                           // ProjectFindFile's own fuzzy-narrowed-picker shape, over
                           // Editor/RecentFiles.h's candidate list instead of a directory
                           // walk (see RefreshFindRecentFileStatus/HandleFindRecentFileKey).
                           FindRecentFile,
                           // BookmarkSetName is a single plain-text prompt (pre-filled
                           // with a default name), fitting the shared HandlePromptKey
                           // else-chain FindScratch/TaskName use -- no dedicated handler.
                           BookmarkSetName,
                           // BookmarkJump is ProjectFindFile's own picker shape again,
                           // over Editor/Bookmark.h's sorted name list -- shared by
                           // bookmark-jump/bookmark-delete, distinguished by
                           // bookmarkPromptAction_ (TaskName's own precedent for
                           // RunTask/CancelTask). See RefreshBookmarkJumpStatus/
                           // HandleBookmarkJumpKey.
                           BookmarkJump };

    enum class DeleteFileStage { EnteringPath,
                                 Confirming };
    enum class RenameFileStage { EnteringSource,
                                 EnteringDestination };
    // property-drawers follow-up: org-set-property's own two linear stages,
    // RenameFileStage's exact shape.
    enum class PropertyPromptStage { EnteringName,
                                     EnteringValue };
    // backup-and-recovery follow-up: recover-file's two linear stages,
    // DeleteFileStage's exact shape (pick, then y/n).
    enum class RecoverFileStage { PickingVersion,
                                  Confirming };
    // task-runner follow-up: which action TaskName's prompt performs on
    // Enter -- Run calls TaskRunner::RunTask (and switches to the resulting
    // buffer), Cancel calls TaskRunner::CancelTask.
    enum class TaskPromptAction { Run,
                                  Cancel };
    // editor-ergonomics follow-up: which action InputMode::BookmarkJump's
    // picker performs on Enter -- TaskPromptAction's own shape.
    enum class BookmarkPromptAction { Jump,
                                      Delete };

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
    // which-key follow-up: builds the popup content for the prefix sequence
    // currently pending in dispatcher_ -- factored out of
    // DispatchChordNormally since it's only ever needed there, right after
    // Feed reports Pending.
    [[nodiscard]] WhichKeyHint BuildWhichKeyHint() const;
    // Vim-mode follow-up: called instead of DispatchChordNormally from the tail of
    // Normal-mode key handling whenever editor::vim::VimModeEnabled() is true.
    // vimEngine_'s own Mode::Insert is the one case that still falls through to
    // DispatchChordNormally underneath (self-insert-command, auto-pair, snippets, ghost
    // completion all keep working unmodified in Insert mode -- see VimEngine.h's own
    // header comment) -- everything else (Normal/Visual/Replace/CommandLine) is consumed
    // by vimEngine_ directly, never reaching Dispatcher::Feed at all. May destroy *this*
    // (a PendingIntent::CloseBuffer forwards to RequestCloseBuffer, same
    // window-management caution as DispatchChordNormally's own doc comment) -- always
    // this call's own return, nothing after.
    bool HandleVimKey(const editor::KeyChord& chord);
    void HandlePrefixArgumentKey(const editor::KeyChord& chord);
    // snippet-expansion follow-up. HandleSnippetKey classifies only: the
    // navigation/end chords it consumes, everything else falls through to
    // DispatchChordNormally as its last statement (nothing after -- the
    // dispatched command can destroy *this*, HandlePrefixArgumentKey's
    // exact constraint); the per-keystroke buffer work (undo group, armed
    // pristine-placeholder delete, mirror sync, end-of-session checks)
    // lives in RunCommandAndHandleOutcome's snippet hooks, where *this* is
    // provably alive and command-driven edits (kill-line, yank, C-u
    // repeats) get identical treatment to plain typing.
    void HandleSnippetKey(const editor::KeyChord& chord);
    // Performs a requested expansion (parse, replace [replaceStart,
    // replaceEnd), start the session) against the active buffer -- the
    // InteractiveRequest::SnippetExpand case and the snippet-format
    // LSP-completion accept path both land here.
    void BeginSnippetExpansion(std::size_t replaceStart, std::size_t replaceEnd, const std::string& body);
    // Clears the session's buffer-side ranges (buffer re-resolved by name
    // -- a buffer closed mid-session is a safe no-op) and resets the
    // members; leaves the expanded text in place.
    void EndSnippetSession();
    // The session buffer, re-resolved by name per use (AsyncFileLoader's
    // own never-hold-a-Buffer* precedent): BufferList::Find first, falling
    // back to the active buffer when its name matches -- a headless
    // BufferView's buffer isn't necessarily in any BufferList. nullptr
    // means the buffer is genuinely gone (closed mid-session).
    [[nodiscard]] text::Buffer* ResolveSnippetBuffer();
    void                        HandleSearchKey(const editor::KeyChord& chord);
    // search_->StatusText() plus a dimmed ghost of lastSearchQuery_ appended
    // when the current query is still empty -- see lastSearchQuery_'s own
    // doc comment.
    [[nodiscard]] std::string SearchStatusText() const;
    void                      HandleQueryReplaceKey(const editor::KeyChord& chord);
    void                      HandleQueryReplaceKeyInner(const editor::KeyChord& chord); // the match-limit-catch split's body half
    void                      HandleConfirmQuitKey(const editor::KeyChord& chord);
    void                      HandlePromptKey(
        const editor::KeyChord&
            chord);        // shared by FindFile/SwitchToBuffer/ProjectSearch/CreateDirectory/FindScratch/StringRectangle -- see prompt_
    void CompletePrompt(); // Tab in HandlePromptKey -- find-file paths, buffer names, or scratch names, by inputMode_
    // minibuffer-composer-cursor-editing follow-up: Left/Right/Home/End/
    // Backspace/Delete/plain-character-insert against prompt_, uniformly,
    // for every prompt handler in this class (not just HandlePromptKey's own
    // shared modes -- ExecuteCommand/ProjectFindFile/FindRecentFile/
    // BookmarkJump/SelectTheme/DocumentSymbol/WorkspaceSymbol/DeleteFile/
    // RenameFile/SetProperty/RecoverFile all drive their own prompt_ key
    // handling directly, each with its own per-keystroke side effects --
    // selection-reset, a fuzzy re-rank, a debounce-armed server re-query --
    // that this helper can't own itself). TextEdited means the caller should
    // run its own post-edit side effects (reset any live selection index,
    // refresh its status/candidate display); CursorMoved means only the
    // cursor itself changed, nothing to re-filter; NotHandled means the
    // caller should fall through to its own handling (Enter, Escape, Up/Down
    // selection, Tab-completion, ...).
    enum class PromptEditOutcome { NotHandled,
                                   CursorMoved,
                                   TextEdited };
    [[nodiscard]] PromptEditOutcome HandlePromptEditingKey(const editor::KeyChord& chord);
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

    // rename-file-notifications follow-up: the actual rename, factored out
    // of HandleRenameFileKey so it can run after EndInteractiveSession()
    // clears renameSource_/renameStage_ -- source/destination are passed by
    // value rather than read back off those members, which an in-flight
    // LspManager::RequestWillRenameFiles round trip (see its own doc
    // comment) could otherwise see reset out from under it by a
    // subsequently-started rename session. When lspManager_ is unset (most
    // tests) or nothing matches, RequestWillRenameFiles's callback fires
    // synchronously with nullopt, so this still completes inline exactly as
    // it did before this follow-up existed.
    void PerformProjectRename(const std::filesystem::path& source, const std::filesystem::path& destination);

    // property-drawers follow-up: org-set-property's own two-stage session,
    // RenameFileStage/HandleRenameFileKey's exact shape -- propertyStage_ ==
    // EnteringName collects the property's name into pendingPropertyName_,
    // re-emplacing prompt_ pre-filled with that property's current value (if
    // any) for propertyStage_ == EnteringValue; the second Enter calls
    // org::SetPropertyAtPoint. Re-resolves HeadlineAtPoint fresh at the
    // final Enter rather than trusting a value captured when the session
    // opened, same reasoning SetHeadlineTags's own doc comment states.
    void HandleSetPropertyKey(const editor::KeyChord& chord);

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
    // pendingCodeActions_ and applies it directly via ResolveAndApplyCodeAction
    // (exactly one action) or enters LspCodeActionSelect (more than one) --
    // inputMode_ is therefore only ever touched from inside this async
    // callback, never eagerly when the request is first sent. Applying
    // without a separate y/n confirmation is deliberate: worst case the user
    // just undoes it, same as any other edit.
    void RequestCodeActionsAtPoint();
    // Renders pendingCodeActions_ as a numbered list into statusMessage_,
    // codeActionSelection_ visually marked -- called after RequestCodeActionsAtPoint
    // first enters LspCodeActionSelect, and again by HandleCodeActionSelectKey
    // whenever Up/Down changes the selection.
    void RefreshCodeActionSelectStatus();
    // Up/Down move codeActionSelection_ (clamped) and refresh; a digit '1'-'9'
    // or Enter applies pendingCodeActions_[codeActionSelection_] directly via
    // ResolveAndApplyCodeAction and ends the session (no separate y/n
    // confirmation -- see RequestCodeActionsAtPoint's doc comment); Escape/C-g
    // cancels back to Normal.
    void HandleCodeActionSelectKey(const editor::KeyChord& chord);
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
    // action.touchesUnsupportedForm (a "documentChanges" WorkspaceEdit --
    // still unparsed, see LspContent.h) or it has no edit to apply.
    // Otherwise resolves action.edits' URIs to real paths
    // (LspManager::ResolveCodeActionEdits, refusing wholesale on any
    // unresolvable one) and opens/finds every touched buffer first, the
    // same all-or-nothing-open guarantee ApplyRename establishes -- a code
    // action's edit can touch more than one file exactly the way a rename
    // can. Hands the result to ApplyProjectEdit, which resolves each
    // buffer's own WorkspaceTextEdit LspPositions to byte offsets against
    // its CURRENT content (safe without a fresh generation check -- the
    // modal Select/Confirm input modes already block ordinary
    // typing/editing for the whole exchange) and applies them.
    void ApplyCodeAction(const editor::lsp::CodeAction& action);
    // code-actions-resolve follow-up (factored out for quick-fix). Sends
    // codeAction/resolve first when the action arrived without its edit
    // (action.resolvable), applying from inside that async callback;
    // otherwise applies directly. Shared by RequestCodeActionsAtPoint,
    // HandleCodeActionSelectKey, and RequestQuickFixAtPoint.
    void ResolveAndApplyCodeAction(const editor::lsp::CodeAction& action);
    // quick-fix follow-up. Same request/staleness-guard shape as
    // RequestCodeActionsAtPoint, but applies the response's single
    // unambiguous fix immediately (a lone action, else a lone isPreferred
    // one, else a lone quickfix-kind one -- undo is the safety net, per the
    // user's own ask), entering the ordinary LspCodeActionSelect session
    // only when no selector produces exactly one candidate.
    void RequestQuickFixAtPoint();

    // codeLens follow-up. Runs the first code lens (LspManager::
    // CodeLensSpans, sorted by startByte) whose range covers point's own
    // line -- a deliberate v1 simplification, not a full disambiguation
    // picker: a line carrying more than one lens only ever runs the
    // first, the same "curated v1 subset" precedent this codebase already
    // has elsewhere (Org priorities capped at A-C). Resolves via
    // codeLens/resolve first when the lens has no command yet
    // (!hasCommand), then always finishes with ExecuteCommand
    // (workspace/executeCommand) -- the same two-step
    // resolve-then-run shape RequestQuickFixAtPoint already uses for code
    // actions.
    void RequestCodeLensAtPoint();

    // declaration/typeDefinition/implementation follow-up: which LSP
    // location-request RequestDefinitionAtPoint sends -- the request/
    // response/jump/select-list handling below is identical for all four
    // (LspManager::ResolvedLocation is the exact same shape every one of
    // RequestDefinition/RequestDeclaration/RequestTypeDefinition/
    // RequestImplementation returns), only the wire method and the
    // human-facing "Requesting .../No ... found." wording differ.
    enum class LspLocationKind { Definition,
                                 Declaration,
                                 TypeDefinition,
                                 Implementation };

    // go-to-definition follow-up. Mirrors RequestCodeActionsAtPoint's own
    // shape exactly: bumps definitionRequestGeneration_, calls
    // LspManager::RequestDefinition, and discards a stale response (buffer/
    // point changed, or a newer request already superseded it) the same
    // way. Zero locations reports "No definition found." via
    // statusMessage_; exactly one jumps directly (JumpToDefinition, no
    // confirmation needed -- unlike a code action, opening a file and
    // moving point is trivially undoable/re-navigable, nothing destructive
    // to confirm); more than one enters LspGotoDefinitionSelect the same
    // way multiple code actions enter LspCodeActionSelect. kind (declaration/
    // typeDefinition/implementation follow-up) selects which of the four
    // LspManager requests above is sent and only changes the wording --
    // pendingLocationLabel_ carries kind's own label through to the async
    // callback for that wording, since the request itself may still be in
    // flight when a *different* kind's request supersedes it.
    void RequestDefinitionAtPoint(LspLocationKind kind = LspLocationKind::Definition);
    void RefreshDefinitionSelectStatus();
    void HandleDefinitionSelectKey(const editor::KeyChord& chord);
    // Opens location.path (BufferList::OpenOrCreateFile, matching
    // VisitSearchResult's own precedent for jumping into a project file)
    // and moves point to location.position, resolved against the newly-
    // opened buffer's own content.
    void JumpToDefinition(const editor::lsp::LspManager::ResolvedLocation& location);

    // One browse session's state: the tree itself (Editor/ExpandableTree.h,
    // NodeData = LspManager::ResolvedHierarchyItem so every node keeps both
    // the item to replay on its own next expand and the resolved path/
    // position to jump to), which of the four requests every expand in this
    // session sends, and the buffer/serverKey pair every request in this
    // session is resolved against -- always the buffer point was in when
    // the session started (RequestHierarchyAtPoint's own bufferPtr), not
    // whichever buffer happens to be active when a later expand fires
    // (activeBuffer_ may have changed, or even be a different buffer
    // entirely, once keyboard focus has moved to the TreeView overlay).
    struct HierarchySession {
        editor::ExpandableTree<editor::lsp::LspManager::ResolvedHierarchyItem> tree;
        HierarchyDirection                                                    direction;
        text::Buffer*                                                         buffer;
        std::string                                                           serverKey;
        std::string                                                           rootName; // for the TreeView's own border title
    };

    // Sent for lsp-call-hierarchy-incoming/-outgoing/lsp-type-hierarchy-
    // supertypes/-subtypes. Sends textDocument/prepareCallHierarchy or
    // .../prepareTypeHierarchy (whichever direction implies) at point;
    // zero items reports "No callable/typed symbol at point."; more than
    // one (a real but rare case -- an overload set, a macro expansion)
    // takes the first, the same precision-vs-scope cut WorkspaceSymbol's
    // own range-less-location fallback already makes elsewhere in this
    // file, rather than adding a whole extra disambiguation step for
    // something this uncommon. On success, seeds a fresh HierarchySession
    // with that one item as the tree's sole root and immediately expands
    // it (ExpandHierarchyNode) -- a hierarchy browser opened to a collapsed
    // root showing only the symbol's own name would make the user perform
    // a manual first expand for no reason, since that's the entire point
    // of having opened it.
    void RequestHierarchyAtPoint(HierarchyDirection direction);

    // Sends whichever of RequestIncomingCalls/RequestOutgoingCalls/
    // RequestSupertypes/RequestSubtypes hierarchySession_->direction
    // implies for the node at index, against hierarchySession_->tree.At(index)
    // .data.item -- a no-op if there's no active session, index is out of
    // range, or the node is already loading. A node whose children were
    // already fetched (ExpandableTree::ChildrenFetched) is expanded
    // without a new request at all (ExpandableTree::SetExpanded) -- a
    // Right-arrow on a previously-collapsed-but-already-explored node
    // should feel instant, not re-issue an LSP round trip for an answer
    // already in hand.
    void ExpandHierarchyNode(std::size_t index);

    // Rebuilds a ui::TreeViewModel from hierarchySession_->tree.FlattenVisible()
    // and fires onHierarchyChanged_ with it -- called after every
    // expand/collapse/selection change, i.e. every mutation to
    // hierarchySession_ or hierarchySelectedIndex_. Fires with std::nullopt
    // instead when there's no active session (hides the overlay).
    void PushHierarchyModel();

    // Ends hierarchySession_ (clears it, fires onHierarchyChanged_(nullopt))
    // and reclaims keyboard focus for this BufferView (TakeFocus()) -- the
    // shared tail of HierarchyActivate/HierarchyCancel, since both close
    // the session the same way, only differing in whether a jump happens
    // first.
    void EndHierarchySession();

    // Debugging wishlist follow-up (pointer/linked-list graph view). Same
    // ExpandableTree-backed, TreeView-rendered browse-session shape as
    // HierarchySession above, over DAP variables instead of LSP hierarchy
    // items -- see Editor/PointerGraphNode.h's own doc comment for why that
    // type (not DapManager::Variable directly) is the tree's NodeData.
    // visitedMemoryRefs is the one thing this session needs that
    // HierarchySession doesn't: a real linked/circular list can point back
    // into itself, unlike an LSP call/type hierarchy (acyclic by
    // construction), so every memoryReference seen so far in this session is
    // tracked to detect and stop a cycle rather than expanding forever.
    struct PointerGraphSession {
        editor::ExpandableTree<editor::PointerGraphNode> tree;
        std::string                                      rootName; // for the TreeView's own border title
        std::set<std::string>                            visitedMemoryRefs;
    };

    // Sent for dap-show-pointer-graph. Guarded on the session being Stopped
    // (LineInspectAtPoint's own guard) and on the current *debug* buffer
    // line parsing to a ParseDebugVariableLine result with
    // variablesReference > 0 (ExpandVariableAtPoint's own "No expandable
    // variable on this line." status message on failure -- this is the same
    // marker every dap-expand-variable invocation already requires). Seeds a
    // fresh PointerGraphSession with that one root node, seeds
    // visitedMemoryRefs with the root's own memoryReference when non-empty,
    // and immediately expands it (ExpandPointerGraphNode) -- same
    // "auto-expand the root" precedent as RequestHierarchyAtPoint.
    void RequestPointerGraphAtPoint();

    // Sends DapManager::RequestVariables against
    // pointerGraphSession_->tree.At(index).data.variablesReference -- a
    // no-op if there's no active session, index is out of range, or the
    // node is already loading. A node whose children were already fetched
    // is expanded without a new request, same as ExpandHierarchyNode. Each
    // returned DapManager::Variable becomes a PointerGraphNode; one whose
    // memoryReference is non-empty and already in visitedMemoryRefs is
    // marked cyclic (and its variablesReference forced to 0, so the tree
    // never offers to expand it again) instead of being added to
    // visitedMemoryRefs and recursed into like every other child.
    void ExpandPointerGraphNode(std::size_t index);

    // Rebuilds a ui::TreeViewModel from pointerGraphSession_->tree.FlattenVisible()
    // (FormatPointerGraphLabel per row) and fires onPointerGraphChanged_ --
    // PushHierarchyModel's own mirror.
    void PushPointerGraphModel();

    // EndHierarchySession's own mirror: clears pointerGraphSession_, fires
    // onPointerGraphChanged_(nullopt), reclaims keyboard focus.
    void EndPointerGraphSession();

    // symbol-search follow-up. Bumps documentSymbolRequestGeneration_ and
    // calls LspManager::RequestDocumentSymbols; discards a stale response
    // the same way RequestDefinitionAtPoint does. Zero symbols reports "No
    // symbols found."; any other count (including one) opens the
    // fuzzy-narrowed InputMode::LspGotoSymbol picker (ProjectFindFile's own
    // shape) rather than jumping directly the way a single go-to-definition
    // result does -- browsing a buffer's outline is worth showing even when
    // there's only one entry, unlike a definition jump.
    void RequestDocumentSymbolsAtPoint();
    void RefreshDocumentSymbolStatus();
    void HandleDocumentSymbolKey(const editor::KeyChord& chord);

    // symbol-search follow-up. Opens InputMode::LspWorkspaceSymbol
    // immediately (StartInteractiveSession's own case) and fires the first
    // workspace/symbol request (an empty query) right away, mirroring
    // ExecuteCommand's "populate right away" precedent. Every subsequent
    // keystroke re-arms workspaceSymbolDebounceTimer_ (LspCompletionDebounceMs(),
    // the same Janet-configurable debounce ghost-text completion already
    // uses -- no new setting for what's the same "don't hammer the server
    // every keystroke" need) rather than sending immediately; the timer's
    // own fired callback re-checks inputMode_ == LspWorkspaceSymbol first
    // (MaybeScheduleAutoCompletion/RequestCompletionAtPoint's own guard
    // shape), since ending the session doesn't cancel an already-armed
    // DeadlineTimer. workspaceSymbolRequestGeneration_ discards a response
    // superseded by a newer request the same way every other async Lsp*
    // session here does.
    void RequestWorkspaceSymbolsForCurrentQuery();
    void RefreshWorkspaceSymbolStatus();
    void HandleWorkspaceSymbolKey(const editor::KeyChord& chord);

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

    // jump-back-stack follow-up: a per-pane back-stack of saved positions,
    // pushed right before every location-jumping command (JumpToDefinition;
    // goto-line's HandlePromptKey branch; HandleBookmarkJumpKey;
    // HandleRegisterKey's JumpToRegister case) and popped by jump-back
    // (InteractiveRequest::JumpBack). Buffer identified by name, not a raw
    // Buffer*, and re-resolved via bufferList_.Find at pop time -- same
    // dangling-reference-avoidance precedent as Editor/Register.h's
    // PointRegisterValue.
    struct JumpMark {
        std::string bufferName;
        std::size_t byteOffset;
    };
    std::vector<JumpMark> jumpBackStack_;
    // jump-back-stack follow-up, forward direction: the mirror-image stack --
    // JumpBack pushes the position it's leaving onto this before jumping,
    // JumpForward pops from it and pushes back onto jumpBackStack_ in turn,
    // so the pair behaves like a browser's back/forward buttons. PushJumpMark
    // clears this whenever a *new* jump is made (goto-definition, goto-line,
    // ...), since branching off mid-history invalidates the old forward path
    // the same way a fresh navigation does in a browser. Evicted the same
    // way and against the same cap as jumpBackStack_ (kMaxJumpBackStack) --
    // no separate constant for it.
    std::vector<JumpMark> jumpForwardStack_;
    void                  PushJumpMark();
    // Pops entries until one resolves to a still-open buffer (skipping any
    // whose buffer has since closed) and jumps there; sets a "No more jump
    // history." status message if the stack is exhausted without finding one.
    void JumpBack();
    // JumpBack's redo direction -- see jumpForwardStack_'s own doc comment.
    void JumpForward();

    // rename follow-up. StartInteractiveSession's LspRename case opens the
    // synchronous "New name: " prompt (inputMode_ = LspRenameNewName);
    // HandlePromptKey's own Enter branch for that mode calls this once the
    // name is typed. Mirrors RequestCodeActionsAtPoint's async/staleness-
    // guard shape once again, but resolves to a full ResolvedRename
    // (potentially many files) rather than a single buffer's edits.
    void RequestRenameAtPoint(const std::string& newName);
    // Thin wrapper over ApplyResolvedWorkspaceEdit below (statusMessage_-only
    // reporting, no return value -- callers driven from a rename response
    // don't need a bool the way the server-push path does).
    void ApplyRename(const editor::lsp::LspManager::ResolvedRename& result);

    // prepareRename follow-up. StartInteractiveSession's LspRename case
    // calls this instead of opening the "New name:" prompt directly: sends
    // textDocument/prepareRename first, then either opens the prompt
    // (prefilled with the symbol's current text when the server returned a
    // usable range/placeholder), reports "not renameable here" without
    // opening it at all (a real, considered server answer), or falls back to
    // the old unprefilled-prompt behavior (a transport error, or a server
    // that simply doesn't implement this optional method).
    void RequestPrepareRenameAtPoint();

    // linked-editing-range follow-up. StartInteractiveSession's
    // LspLinkedEditingRange case calls this: sends textDocument/
    // linkedEditingRange and, on a usable response, starts linkedEditingSession_.
    // Refuses outright (statusMessage_, no request sent) while a real
    // snippet session is live -- see linkedEditingSession_'s own doc
    // comment for why the two can never coexist.
    void RequestLinkedEditingRangeAtPoint();
    // Resolve-by-name, not-a-raw-pointer lookup mirroring ResolveSnippetBuffer's
    // own contract exactly -- a buffer closed mid-session is a safe no-op.
    text::Buffer* ResolveLinkedEditingBuffer();
    // Clears linkedEditingSession_ (and its buffer-side ranges, if the
    // buffer is still resolvable) -- called from EndInteractiveSession
    // (mirroring EndSnippetSession's own inclusion there) and from
    // RunCommandAndHandleOutcome's post-dispatch hook once the session's own
    // end conditions are met.
    void EndLinkedEditingSession();

    // edit-application-gaps follow-up: the shared tail ApplyRename,
    // ApplyCodeAction, and ApplyServerPushedWorkspaceEdit all fold into.
    // Refuses (statusMessage_, no mutation anywhere) if
    // edit.touchesUnsupportedForm or it has no edit at all. Otherwise walks
    // edit.edits ("changes" form) and edit.documentChangeOps
    // ("documentChanges" form) IN ORDER: a CreateFile/RenameFile/DeleteFile
    // op takes effect immediately (via BufferList::OpenOrCreateFile/
    // Editor/ProjectFileOps.h, since a later EditFile op in the same
    // sequence may target the file it just created or renamed), while every
    // EditFile op's own edits are collected per-buffer alongside edit.edits
    // and applied together at the end via ApplyProjectEdit -- one
    // undo/project-undo transaction for the whole operation, resource ops
    // included. Bails out -- applying nothing -- the moment any one step
    // fails (an unresolvable/already-existing/missing target), reported via
    // ReportError; returns whether anything was actually applied. Every
    // affected buffer is left modified-but-unsaved like any other in-editor
    // edit -- no auto-save-across-files behavior, matching this codebase's
    // existing "saves are always user-initiated" convention.
    bool ApplyResolvedWorkspaceEdit(const editor::lsp::LspManager::ResolvedRename& edit, std::string description);

    // project-undo follow-up: applies one WorkspaceTextEdit list per buffer
    // (via the file-local ApplyWorkspaceTextEdits helper, one undo group
    // each) and, if more than one buffer is touched, records the whole set
    // as one ProjectUndoManager transaction (a no-op if SetProjectUndo was
    // never called) -- the shared tail ApplyRename and a cross-file
    // ApplyCodeAction both fold into, so a project-wide undo/redo covers
    // either source uniformly. `description` becomes the status message and
    // (for a multi-buffer edit) the transaction's own label surfaced later
    // by undo/redo.
    void ApplyProjectEdit(const std::vector<std::pair<text::Buffer*, std::vector<editor::lsp::WorkspaceTextEdit>>>& perBufferEdits,
                          std::string description);

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

    // org-capture follow-up: same one-character-read shape as
    // HandleRegisterKey above -- the character picks a registered
    // Editor/OrgCapture.h template by key, which is then expanded straight
    // into its target file/buffer (switching the active pane to it) rather
    // than prompting further.
    void HandleOrgCaptureKey(const editor::KeyChord& chord);

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

    // editor-ergonomics follow-up: HandleProjectFindFileKey/
    // RefreshProjectFindFileStatus's own shape, over
    // recentFileCandidates_ (Editor/RecentFiles.h's most-recent-first path
    // list, cached once per session the same way) instead of a project
    // directory walk. Opens the selected path directly (it's already
    // absolute, unlike ProjectFindFile's project-relative candidates).
    void HandleFindRecentFileKey(const editor::KeyChord& chord);
    void RefreshFindRecentFileStatus();

    // named-projects follow-up: HandleProjectFindFileKey/
    // RefreshProjectFindFileStatus's own shape, over switchProjectEntries_
    // (Editor/ProjectRegistry.h's saved-project list, cached once per
    // session like the file list) instead of a project directory walk --
    // candidates are formatted "name — root" strings; Enter resolves the
    // selected one back to its entry and calls ActivateProjectAndReport.
    void HandleSwitchProjectKey(const editor::KeyChord& chord);
    void RefreshSwitchProjectStatus();

    // dropdown-path-completion follow-up: HandleSwitchProjectKey's own
    // shape, over every open buffer's name -- there's no "create a new
    // buffer by typing a name that doesn't exist" case, so Enter resolves
    // the highlighted ranked candidate.
    void HandleSwitchToBufferKey(const editor::KeyChord& chord);
    void RefreshSwitchToBufferStatus();

    // dropdown-path-completion follow-up: same shape, over
    // vcsBranchCandidates_ (populated before this mode is entered -- see
    // BeginVcsSwitchBranchPrompt). VcsCreateBranch stays on HandlePromptKey's
    // own literal-text path (free-text new-branch naming).
    void HandleVcsSwitchBranchKey(const editor::KeyChord& chord);
    void RefreshVcsSwitchBranchStatus();

    // dropdown-path-completion follow-up: same shape, over
    // editor::acp::AcpAgentNames().
    void HandleAcpAgentNameKey(const editor::KeyChord& chord);
    void RefreshAcpAgentNameStatus();

    // dropdown-path-completion follow-up: FindFile/OpenProjectPath/
    // FindScratch's shared candidate source (GatherPathCompletionCandidates)
    // and live popup refresh (RefreshPathCompletionPopup) -- unlike the pairs
    // just above, Enter for these three still finalizes on literal
    // prompt_->Text() in HandlePromptKey (typing a path/name with no match is
    // a valid "create new" action), so there is no HandleXKey of their own --
    // just Up/Down/Tab handling inline in HandlePromptKey.
    [[nodiscard]] std::vector<std::string> GatherPathCompletionCandidates() const;
    void RefreshPathCompletionPopup();

    // named-projects follow-up: the shared tail of switch-project/
    // open-project once a target root is known -- runs
    // editor::ActivateProjectRoot's real priority chain and reports the
    // outcome, including calling eventLoop_->Exit() for the replace-in-place
    // case (HandleConfirmQuitKey's own 'y'-branch precedent -- ned itself
    // performs the actual execv() from main.cpp, once this function's own
    // stack has fully unwound).
    void ActivateProjectAndReport(const std::filesystem::path& root);

    // editor-ergonomics follow-up: same picker shape again, over
    // bookmarkCandidates_ (Editor/Bookmark.h's sorted name list). Enter
    // either jumps (opening the bookmark's file if not already open and
    // moving point) or deletes, per bookmarkPromptAction_.
    void HandleBookmarkJumpKey(const editor::KeyChord& chord);
    void RefreshBookmarkJumpStatus();

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
    // not a prompt session -- doesn't touch inputMode_. Both this and
    // VisitVcsResult below now delegate to the shared VisitResultUnderPoint,
    // so either command/chord behaves identically in any results-style
    // buffer -- multibuffer-visit-unification follow-up: C-c C-v used to be a
    // silent no-op in a *vcs diff*/*diagnostics*/*references* buffer since it
    // never checked MultibufferIndexFor, only the "path:line:" regex.
    void VisitSearchResult();

    // VCS blame gutter follow-up: same delegation as VisitSearchResult above
    // -- kept as a separate method/command (vcs-visit-result, C-c v v) for
    // existing keybinding/Janet-name compatibility, not because the logic
    // differs anymore.
    void VisitVcsResult();

    // The actual shared logic both Visit* methods above (and Enter/click on
    // a read-only buffer) delegate to: try MultibufferIndexFor first (works
    // from anywhere inside an excerpt's header/body, not just an index
    // line), falling back to the "path:line:" regex JumpToPathLine's callers
    // all write. A silent no-op if neither matches -- what makes it safe to
    // invoke unconditionally regardless of which buffer happens to be
    // active.
    void VisitResultUnderPoint();

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

    // scheduling/recurrence follow-up: org-agenda's own entry point -- a
    // sectioned Editor/Multibuffer.h view (one excerpt per
    // editor::AgendaItem, grouped Overdue/Today/Upcoming/Undated per
    // editor::CollectAgendaItems' own sort order) rather than the flat
    // BuildResultsBuffer list this used to build before real SCHEDULED:/
    // DEADLINE: timestamp parsing existed. Same "no source, no jump" no-op
    // posture every other multibuffer consumer here already has.
    void BuildAgendaMultibuffer();

    // org-clock-display follow-up: org-clock-report's own entry point --
    // same Editor/Multibuffer.h shape as BuildAgendaMultibuffer, but scoped
    // to the active buffer only (one excerpt per headline with a nonzero
    // own-or-subtree clocked total, in file order), not project-wide --
    // matches org-clock-in/-out's own buffer-scoped posture.
    void BuildClockReportMultibuffer();

    // diagnostics-log follow-up: show-messages's own entry point --
    // rebuilds "*Messages*" from Editor/DiagnosticsLog.h's in-memory ring
    // (currently-visible categories only) and switches this pane's active
    // buffer to it, same one-liner shape RunTask-adjacent buffer switches
    // use elsewhere.
    void ShowMessagesBuffer();

    // find-all-references follow-up: project-find-references's entry point.
    // Finds the ASCII word/identifier region at point (a local scan, same
    // classification as Commands.cpp's own WordRegionAt -- see that
    // function's doc comment for why this is a small duplicated copy rather
    // than a new shared seam), then picks one of two sources for the result
    // list, stitched into "*references: <word>*" via Editor/Multibuffer.h's
    // BuildMultibuffer either way -- vcs-visit-result already jumps to
    // source from any excerpt, same as RequestVcsFullDiffBuffer/
    // RequestDiagnosticsBuffer's own buffers.
    //
    // find-references follow-up: when a language server is actually running
    // for this buffer (StatusForLanguage == Running, the same "is one
    // currently usable" check RequestCompletionAtPoint's dabbrev-fallback
    // uses), sends a real, async textDocument/references and builds one
    // excerpt per ResolvedLocation (reading the target line straight off
    // disk -- ReadFileLine -- since a reference's file need not be open).
    // Otherwise (nothing configured, still spawning, crashed) falls back to
    // the original synchronous path: a whole-word RE2 pattern
    // ("\\bword\\b" -- safe to embed unescaped, the word-scan only ever
    // admits [A-Za-z0-9_], none of them regex metacharacters) through
    // SearchDirectory(ProjectRoot(), ...), a fast textual approximation
    // (matches inside comments/strings too, can't tell one same-named
    // symbol from another in a different scope). Mirrors SwitchHeaderSource's
    // own "LSP is a nice-to-have accelerant, not the only path" precedent --
    // unlike lsp-goto-definition/-declaration/etc., which refuse outright
    // with no running server, this command already has a working universal
    // fallback worth keeping rather than a hard LSP requirement.
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
    // Hunk-navigation follow-up (vcs-next-hunk/vcs-previous-hunk): moves
    // point to the next/previous entry in diffHunkStartLines_ relative to
    // point's own current line, wrapping neither direction (an empty/
    // exhausted search is a status-message no-op, not a wrap-around --
    // matches next-error's own "you've reached the end" convention rather
    // than isearch's wrap-and-continue one, since there's no obvious
    // "current" hunk to resume scanning from after a wrap here).
    void JumpToNextHunk();
    void JumpToPreviousHunk();
    // next-error follow-up: JumpToNextHunk/JumpToPreviousHunk's own
    // walk-with-no-wrap shape, generalized over whichever results buffer
    // Editor/NextError.h's LastResultsBuffer() names instead of a private
    // per-pane cache -- StepError resolves the buffer, collects its
    // locations, and asks StepResultLocation (its own process-wide cursor,
    // not this pane's point) for the next/previous one to jump to.
    void NextError();
    void PreviousError();
    void StepError(bool forward);
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
    void BeginVcsCreateBranchPrompt(); // VCS side panel follow-up -- see BufferView.cpp's own comment

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

    // test-runner integration: self-ensuring, SymbolGutterActive's exact
    // shape (calls EnsureTestGutterCache() itself -- the width math needs
    // it current within this same Paint() call). Active only when the mode
    // has test discovery, the buffer is an ordinary editable one, markers
    // were actually discovered, and a parsed TestRunner outcome exists to
    // match them against -- "no results yet" costs zero gutter width.
    [[nodiscard]] bool TestGutterActive() const;

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

    // DAP round 2: dap-remove-watch's body -- parses a "[watch:N]" trailing
    // marker off point's own "*debug*" buffer line (ExpandVariableAtPoint's
    // "[ref:N]" convention, applied to watch lines), removes that watch,
    // and re-runs ShowDebugInfo() to refresh.
    void RemoveWatchAtPoint();

    // DAP round 4: dap-restart-frame's body -- parses a "[frame:N]" trailing
    // marker off point's own "*debug*" buffer stack line (ShowDebugInfo's
    // own convention, ExpandVariableAtPoint's "[ref:N]" shape), then calls
    // DapManager::RestartFrame directly -- synchronous status string, no
    // async splice needed (the new position arrives via the next `stopped`
    // event like every other step).
    void RestartFrameAtPoint();

    // DAP round 2: dap-set-variable's body -- parses point's own "*debug*"
    // buffer line for its "[owner:M]" container-reference marker (see
    // FormatDebugVariableLine) and variable name, prompts for a new value
    // (InputMode::DapSetVariableValue), and on Enter sends the DAP
    // setVariable request, splicing the result back into the exact line
    // via the same staleness-guarded DeleteRange+InsertAt ExpandVariableAtPoint
    // uses.
    void SetVariableAtPoint();

    // Debugging wishlist: dap-toggle-hex-format's body -- parses point's own
    // "*debug*" buffer line for either a "[watch:N]" or a "[owner:M]"
    // marker (RemoveWatchAtPoint's/SetVariableAtPoint's own marker
    // conventions), re-fetches just that one value with DAP's `format:
    // {hex: true}` hint (Evaluate/RequestVariables's new hex parameter),
    // and splices the reformatted line back in place -- the exact
    // staleness-guarded DeleteRange+InsertAt shape SetVariableAtPoint's own
    // callback uses. A trailing "[hex]" marker this appends is the toggle's
    // only state: present means "flip back to decimal next time," absent
    // means "switch to hex."
    void ToggleHexFormatAtPoint();

    // Debugging wishlist: dap-toggle-watch-graph's body -- parses point's
    // own "*debug*" buffer line for a "[watch:N]" marker (RemoveWatchAtPoint's
    // convention); unlike ToggleHexFormatAtPoint this only applies to watch
    // lines, not arbitrary variable lines -- "graph this" only makes sense
    // for a user-named watch expression. A watch with recorded scalar
    // history (DapManager::WatchHistoryAt) gets Editor/Sparkline.h's
    // block-glyph sparkline of its values across recent stops appended; a
    // watch with no history yet but an expandable current value
    // (variablesReference > 0, via EvaluateWithReference) instead does a
    // one-shot RequestVariables expansion and, if every child value parses
    // as numeric, renders the same helper as a bar chart of the array's
    // current elements. A trailing "[graph]" marker is this toggle's only
    // state, ToggleHexFormatAtPoint's own "present means flip back off"
    // convention.
    void ToggleWatchGraphAtPoint();

    // DAP round 5: dap-show-disassembly's body -- reuses RestartFrameAtPoint's
    // own "[frame:N]" marker parse (point's current line, any buffer -- no
    // check that it's actually the *debug* buffer, same lack of a check
    // every other marker-parsing Dap method here has) to pick a frame,
    // falling back to RequestStackTrace's top frame when there's no marker
    // or it names an id RequestStackTrace's own fresh result doesn't
    // contain. Chains stackTrace -> disassemble (a fixed 64-instruction
    // window centered on the frame's instructionPointerReference) and
    // builds/switches to a read-only "*disassembly*" buffer.
    void ShowDisassemblyAtPoint();
    void BuildDisassemblyBuffer(const std::vector<editor::dap::DapManager::DisassembledInstruction>& instructions,
                                const std::string&                                                   pcAddress);

    // DAP round 5: dap-show-memory-at-point's body -- parses a "[mem:<ref>]"
    // trailing marker off point's own "*debug*" buffer variable line
    // (FormatDebugVariableLine's fourth marker, alongside "[ref:N]"/
    // "[owner:M]"), then prompts for a byte count (InputMode::
    // DapMemoryByteCount, empty/unparsable input defaults to 128) before
    // sending the DAP readMemory request and building a read-only
    // "*memory*" hex-dump buffer.
    void ShowMemoryAtPoint();
    void BuildMemoryBuffer(const std::string& memoryReference, const editor::dap::DapManager::MemoryBlock& block);

    // DAP round 2: dap-select-thread's entry point -- fetches the current
    // thread list (RequestThreads) and, when non-empty, enters
    // InputMode::DapThreadSelect (LspCodeActionSelect's numbered-choice
    // shape, driven by pendingDapThreads_/dapThreadSelection_).
    void BeginDapThreadSelect();
    void RefreshDapThreadSelectStatus();
    void HandleDapThreadSelectKey(const editor::KeyChord& chord);

    // DAP round 3: dap-select-exception-breakpoints's entry point --
    // BeginDapThreadSelect's shape, but multi-select/toggle over
    // DapManager::AvailableExceptionFilters() rather than a single pick;
    // pendingDapExceptionFilters_/dapExceptionFilterSelection_/
    // pendingDapEnabledExceptionFilters_ are the driving state (see their
    // own declarations below).
    void BeginDapExceptionFilterSelect();
    void RefreshDapExceptionFilterStatus();
    void HandleDapExceptionFilterSelectKey(const editor::KeyChord& chord);

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
    // huge-file-structural-gutters follow-up: [start, end) byte window
    // EnsureFoldableBlocksCache/EnsureSymbolGutterCache/EnsureTestGutterCache
    // should actually run mode_.fold/mode_.symbolKind/mode_.testDiscovery
    // against -- {0, content.ByteLength()} (the whole buffer, unchanged
    // behavior) for an ordinary buffer, or a bounded region around the
    // currently-visible viewport (topLine_/size().height, expanded by
    // editor::HugeStructuralWindowBytes() on each side) for a huge
    // (ITextStorage::IsHuge()) one, so a multi-GB buffer's structural
    // gutters never materialize/reparse the whole document. Mirrors
    // Editor/HugeRegexScan.h's own windowing approach; a fold region/symbol/
    // test whose start or end lies outside this window on a huge buffer
    // simply isn't found until scrolling brings it closer -- the same
    // accepted trade-off huge-file-regex-replace/huge-file-vim-search
    // already documented for search.
    [[nodiscard]] std::pair<std::size_t, std::size_t> HugeStructuralWindow(const text::ITextStorage& content) const;
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
    // test-runner integration: (re)derives testGutterLineStatuses_ from
    // mode_.testDiscovery(buffer.Text()) matched against the TestRunner's
    // latest parsed outcome (MatchesTestName, TestRun/TestResult.h) --
    // EnsureSymbolGutterCache's shape with the extra outcome-generation
    // stamp; see the member's own comment below.
    void EnsureTestGutterCache() const;
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
    // snippet-expansion follow-up: whether byteOffset falls inside the live
    // session's active tabstop field -- InIsearchMatch's exact per-cell
    // paint-loop shape, backing the snippetFieldBackground wash.
    [[nodiscard]] bool InActiveSnippetField(std::size_t byteOffset) const;
    // Debugging wishlist (line-inspect follow-up): whether byteOffset falls
    // inside one of the sub-expression spans dap-line-inspect last fanned
    // out evaluate requests for -- InIsearchMatch's exact per-cell paint-loop
    // shape, backing the lineInspectBackground wash.
    [[nodiscard]] bool InLineInspectHighlight(std::size_t byteOffset) const;

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
    // main-editor-sticky-scroll follow-up: how many pinned breadcrumb rows
    // the MOST RECENT Paint() call reserved at the top of this pane -- live
    // state read back by CursorPosition()/ByteOffsetForPoint() so the
    // terminal cursor, popup anchors, and mouse-click row resolution all
    // agree with what Paint() actually drew this frame (real content starts
    // stickyRowCount_ rows below the pane's own top edge whenever this is
    // nonzero). Same "not a Paint()-local, a real always-current member"
    // shape topLine_ itself already is. 0 whenever sticky scroll is
    // disabled, the mode has no tags query, or nothing is scrolled far
    // enough to pin anything yet.
    int stickyRowCount_ = 0;
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

    std::size_t                  dragAnchor_ = 0;            // point position at the last mouse press, for drag-selection
    // Double/triple-click word/line selection: mirrors ProjectSidebar's own
    // lastFileClickPath_/lastFileClickTime_ double-click detection, extended
    // with a click count so a third click within the window selects the
    // whole line rather than re-selecting the word.
    std::optional<std::size_t>   lastClickOffset_;
    std::chrono::steady_clock::time_point lastClickTime_{};
    int                           clickCount_ = 0;
    std::optional<std::string>   debugMouseLogPath_;         // see LogMouseEvent
    ScrollBar*                   scrollBar_       = nullptr; // see SetScrollBar
    ScrollArrowButton*           scrollUpArrow_   = nullptr; // see SetScrollArrows
    ScrollArrowButton*           scrollDownArrow_ = nullptr;
    ProjectSidebar*              projectSidebar_  = nullptr;     // see SetProjectSidebar
    VcsPanel*                    vcsPanel_        = nullptr;     // see SetVcsPanel
    std::function<bool()>        splitResizeQuery_;              // see SetSplitResizeQuery
    Minimap*                     minimap_             = nullptr; // see SetMinimap
    Widget*                      minimapScrollColumn_ = nullptr; // see SetMinimap
    editor::lsp::LspManager*     lspManager_          = nullptr; // see SetLspManager
    editor::tasks::TaskRunner*   taskRunner_          = nullptr; // see SetTaskRunner
    editor::ProjectUndoManager*  projectUndo_         = nullptr; // see SetProjectUndo
    editor::testrun::TestRunner* testRunner_          = nullptr; // see SetTestRunner
    editor::vcs::VcsRunner*      vcsRunner_           = nullptr; // see SetVcsRunner
    editor::dap::DapManager*     dapManager_          = nullptr; // see SetDapManager
    editor::acp::AcpManager*     acpManager_          = nullptr; // see SetAcpManager
    const janet::Environment*    janetEnv_            = nullptr; // see SetJanetEnvironment
    bool                         surfaceUnseenLogEntries_ = false; // see SetSurfaceUnseenLogEntries

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

    // DAP round 2: valid only while inputMode_ == InputMode::DapThreadSelect
    // -- same "populated by the entry point, consumed by
    // Refresh*/Handle*Key" convention pendingAcpPermissionOptions_ above
    // establishes.
    std::vector<editor::dap::DapManager::Thread> pendingDapThreads_;
    std::size_t                                  dapThreadSelection_ = 0;

    // DAP round 3: valid only while inputMode_ ==
    // InputMode::DapExceptionFilterSelect -- pendingDapThreads_'s own
    // convention, but the enabled set is a local editable copy (toggled by
    // Space/digit as the user browses) rather than DapManager's own live
    // state, committed via SetExceptionBreakpointFilters only on Enter.
    std::vector<editor::dap::DapManager::ExceptionFilter> pendingDapExceptionFilters_;
    std::set<std::string>                                 pendingDapEnabledExceptionFilters_;
    std::size_t                                           dapExceptionFilterSelection_ = 0;

    // DAP round 2: the path:line a dap-set-breakpoint-condition/
    // dap-set-breakpoint-log-message prompt targets -- captured when the
    // command runs (point's own line, mirroring dap-toggle-breakpoint),
    // consumed on Enter inside HandlePromptKey. Valid only while inputMode_
    // is DapBreakpointCondition/DapBreakpointLogMessage. DAP round 3:
    // DapBreakpointHitCondition joins the same two modes/same struct.
    struct PendingDapBreakpointTarget {
        std::filesystem::path path;
        std::size_t            line = 0;
    };
    std::optional<PendingDapBreakpointTarget> pendingDapBreakpointTarget_;

    // DAP round 2: dap-set-variable's captured target -- buffer/line
    // identity plus the exact line text (ExpandVariableAtPoint's own
    // staleness-guard convention) and the parsed owner reference/variable
    // name. Valid only while inputMode_ == DapSetVariableValue.
    struct PendingDapSetVariable {
        text::Buffer* buffer = nullptr;
        std::size_t    line  = 0;
        std::string    lineText;
        int            ownerRef = 0;
        std::string    name;
    };
    std::optional<PendingDapSetVariable> pendingDapSetVariable_;

    // DAP round 5: dap-show-memory-at-point's captured target -- just the
    // memory reference string itself (unlike PendingDapSetVariable, the
    // result lands in a brand-new "*memory*" buffer, not spliced back into
    // the source line, so no buffer/line/staleness bookkeeping is needed).
    // Valid only while inputMode_ == DapMemoryByteCount.
    std::optional<std::string> pendingDapMemoryReference_;

    EventLoop* eventLoop_ = nullptr; // see SetEventLoop

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
    // Vim-mode follow-up: one VimEngine per pane, always constructed (cheap) but only
    // ever driven when editor::vim::VimModeEnabled() is true -- read live each keystroke
    // rather than cached, matching every other process-wide setting's own convention.
    // See HandleVimKey's own doc comment for the Normal/Visual/Replace-vs-Insert split.
    editor::vim::VimEngine vimEngine_;
    // snippet-expansion follow-up: the live tabstop session (see
    // InputMode::Snippet above). pendingSnippetExpansion_ carries a
    // command's CommandContext::snippetExpansion from
    // RunCommandAndHandleOutcome's stash (the pendingZapToCharAppend_
    // shape) to StartInteractiveSession's SnippetExpand case;
    // snippetPendingPristineDelete_ arms HandleSnippetKey's
    // "first typed character replaces the pristine placeholder" delete for
    // the pre-dispatch hook to apply.
    std::optional<editor::SnippetSession>                          snippetSession_;
    std::optional<editor::CommandContext::SnippetExpansionRequest> pendingSnippetExpansion_;
    bool                                                           snippetPendingPristineDelete_ = false;
    // linked-editing-range follow-up: the live mirror session lsp-linked-
    // editing-range starts (see InteractiveRequest::LspLinkedEditingRange's
    // own doc comment in Command.h). Deliberately not gated behind a
    // distinct InputMode the way InputMode::Snippet gates snippetSession_ --
    // this must keep mirroring edits during ordinary Normal-mode typing, so
    // RunCommandAndHandleOutcome's own post-dispatch hook checks
    // linkedEditingSession_.has_value() directly instead. Mutually exclusive
    // with snippetSession_ (both use Buffer::SnippetRanges_ as their
    // storage) -- RequestLinkedEditingRangeAtPoint refuses to start one
    // while a real snippet session is live, and BeginSnippetExpansion ends
    // any live linked-editing session first.
    std::optional<editor::LinkedEditingSession> linkedEditingSession_;
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
    PropertyPromptStage   propertyStage_ = PropertyPromptStage::EnteringName;
    std::string           pendingPropertyName_; // name entered in PropertyPromptStage::EnteringName

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

    // editor-ergonomics follow-up: projectFindFileCandidates_'s own pair,
    // populated from editor::RecentFilePaths() when the session starts.
    std::vector<std::string> recentFileCandidates_;
    std::size_t              recentFileSelection_ = 0;

    // named-projects follow-up: switch-project's own pair, populated from
    // editor::ListProjects() when the session starts -- entries, not
    // pre-formatted strings, since Enter needs the underlying root back,
    // not just the "name — root" display text FuzzyFilterAndRank ranks.
    std::vector<editor::ProjectRegistryEntry> switchProjectEntries_;
    std::size_t                               switchProjectSelection_ = 0;

    // dropdown-path-completion follow-up: selection index for each of the
    // three newly-dedicated fuzzy-dropdown sessions -- switchProjectSelection_'s
    // own shape, one member per mode since that's this file's established
    // convention even though only one of these (or switchProjectSelection_
    // itself) is ever live at a time.
    std::size_t switchToBufferSelection_  = 0;
    std::size_t vcsSwitchBranchSelection_ = 0;
    std::size_t acpAgentNameSelection_    = 0;
    // dropdown-path-completion follow-up: shared across FindFile/
    // OpenProjectPath/FindScratch (see GatherPathCompletionCandidates'/
    // RefreshPathCompletionPopup's own doc comments) -- one member is enough
    // since exactly one of these three modes is ever live at a time.
    std::size_t pathCompletionSelection_ = 0;

    // named-projects follow-up: the root open-project's path step resolved,
    // held across the transition to its own second (name) prompt --
    // pendingPropertyName_'s own "state carried between two prompts in the
    // same session" shape above.
    std::filesystem::path pendingOpenProjectRoot_;

    // editor-ergonomics follow-up: same pair again, over
    // editor::BookmarkNames(); bookmarkPromptAction_ distinguishes
    // bookmark-jump from bookmark-delete on the same InputMode::
    // BookmarkJump session (TaskPromptAction's own precedent).
    std::vector<std::string> bookmarkCandidates_;
    std::size_t              bookmarkSelection_    = 0;
    BookmarkPromptAction     bookmarkPromptAction_ = BookmarkPromptAction::Jump;

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
    std::function<void()>                           onAcpRewindRequest_;    // see SetOnAcpRewindRequest
    std::function<void()>                           onDapConsoleToggle_;    // see SetOnDapConsoleToggle
    std::function<void()>                           onBufferListToggle_;    // see SetOnBufferListToggle
    std::function<void(text::Buffer&)>              onActiveBufferChanged_; // see SetOnActiveBufferChanged
    std::function<void(std::optional<WhichKeyHint>)> onPrefixHintChanged_;  // see SetOnPrefixHintChanged
    std::function<void(std::optional<ListPopupModel>)> onCandidatesChanged_; // see SetOnCandidatesChanged
    std::function<void(std::optional<ListPopupModel>)> onCompletionChanged_; // see SetOnCompletionChanged

    // call/type-hierarchy follow-up: onHierarchyChanged_ mirrors
    // onCandidatesChanged_'s own role, for the shared TreeView overlay
    // (see SetOnHierarchyChanged's own doc comment). hierarchySession_ is
    // unset whenever no browse session is active; hierarchySelectedIndex_
    // is this BufferView's own record of the TreeView's current selection
    // (TreeView owns navigation directly once focused, so this is only
    // ever *written* by HierarchySelectionChanged/HierarchyActivate/
    // HierarchyToggleExpand/HierarchyCollapse's own index arguments -- see
    // TreeView::SetOnSelectionChanged's own doc comment for why this needs
    // tracking at all) -- carried into every PushHierarchyModel rebuild so
    // an expand/collapse elsewhere in the tree never resets the user's own
    // place in it. hierarchyRequestGeneration_ is definitionRequestGeneration_'s
    // own staleness-guard shape, shared by every request a session sends
    // (prepare and every subsequent expand alike -- only one can be
    // meaningfully in flight at a time regardless of which node it's for).
    std::function<void(std::optional<ui::TreeViewModel>)> onHierarchyChanged_;
    std::optional<HierarchySession>                       hierarchySession_;
    std::size_t                                           hierarchySelectedIndex_    = 0;
    std::size_t                                           hierarchyRequestGeneration_ = 0;

    // Debugging wishlist follow-up (pointer/linked-list graph view): the
    // above five fields' own mirror for PointerGraphSession -- see that
    // struct's own doc comment.
    std::function<void(std::optional<ui::TreeViewModel>)> onPointerGraphChanged_;
    std::optional<PointerGraphSession>                    pointerGraphSession_;
    std::size_t                                           pointerGraphSelectedIndex_     = 0;
    std::size_t                                           pointerGraphRequestGeneration_ = 0;

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

    // semanticTokens follow-up: LspManager::SemanticTokensGeneration(buffer)
    // at the moment this cache entry was last built -- a third staleness
    // check alongside content/class generation, since an LSP response can
    // arrive (and change what should render) with no buffer edit at all.
    // 0 (LspManager's own "never had a response applied" value) when
    // lspManager_ is unset, so every existing test/construction path that
    // never wires it behaves exactly as before -- see LspManager-sourced
    // spans' own appending comment at this cache's build site.
    std::size_t highlightCacheSemanticTokensGeneration_ = 0;

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
        std::size_t                        contentGeneration        = 0;
        std::size_t                        classGeneration          = 0;
        std::size_t                        semanticTokensGeneration = 0;
        std::string                        modeName;
        std::vector<editor::HighlightSpan> spans;
    };
    std::unordered_map<text::Buffer*, HighlightCacheEntry> highlightCacheByBuffer_;

    // embedded-language-documents follow-up: caches mode_.embeddedRegions'
    // resolved documents per buffer, same staleness check/eviction shape as
    // HighlightCacheEntry above -- what EnsureEmbeddedDocumentCache
    // populates once per actually-changed Paint() call, consumed both by
    // Paint()'s own LspManager::SyncEmbeddedDocuments call and by
    // EmbeddedLanguageAtPoint()/ResolvedLspServerKey() below (a cheap lookup
    // into this cache, not a fresh tree-sitter walk per request/keystroke).
    // Empty (erased) whenever mode_.embeddedRegions itself is unset -- every
    // bundled mode but html-mode.
    struct EmbeddedDocumentCacheEntry {
        std::size_t                           contentGeneration = 0;
        std::string                           modeName;
        std::vector<editor::EmbeddedDocument> documents;
    };
    std::unordered_map<text::Buffer*, EmbeddedDocumentCacheEntry> embeddedDocumentCacheByBuffer_;
    void                                                          EnsureEmbeddedDocumentCache();

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
    // huge-file-structural-gutters follow-up: the [start, end) window the
    // cached foldableBlocksCache_ was actually computed against -- see
    // HugeStructuralWindow's own doc comment. Always {0, ByteLength()} for
    // an ordinary buffer (and so never invalidates anything beyond what
    // foldableBlocksCacheGeneration_ already would), only meaningfully
    // narrower for a huge one.
    mutable std::size_t                                      foldableBlocksCacheWindowStart_ = 0;
    mutable std::size_t                                      foldableBlocksCacheWindowEnd_   = 0;
    mutable std::vector<std::pair<std::size_t, std::size_t>> foldableBlocksCache_;
    // per-buffer-highlight-cache follow-up: same persistence-across-a-switch
    // fix as highlightCacheByBuffer_ above, for mode_.fold instead of
    // mode_.highlight -- see that member's own doc comment for the full
    // reasoning (single-slot eviction on switch, modeName's role, and the
    // ClearBufferCaches/close-funnel cleanup).
    struct FoldableBlocksCacheEntry {
        std::size_t                                      contentGeneration = 0;
        std::string                                      modeName;
        // huge-file-structural-gutters follow-up: see
        // foldableBlocksCacheWindowStart_/End_ above -- persisted per buffer
        // too, so switching back to a huge buffer whose viewport (and so
        // window) has since moved correctly recomputes instead of reusing a
        // stale-window entry.
        std::size_t                                       windowStart = 0;
        std::size_t                                       windowEnd   = 0;
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

    // test-runner integration: the per-test pass/fail column, reserved (to
    // the symbol column's immediate left) only while TestGutterActive() --
    // same data-driven no-data-no-width policy.
    static constexpr std::size_t kTestWidth = 1;

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
    // huge-file-structural-gutters follow-up: foldableBlocksCache_ can now
    // change out from under this cache without either ContentGeneration()
    // or FoldGeneration() moving at all -- purely from
    // editor::HugeStructuralWindowBytes()/the viewport moving on a huge
    // buffer, which EnsureFoldableBlocksCache() (called just above, first
    // thing) already tracks via its own foldableBlocksCacheWindowStart_/
    // End_. Mirrored here too, or a window-only change (no content/fold
    // edit at all) would leave this cache silently stale.
    mutable std::size_t                  foldGutterCacheWindowStart_       = 0;
    mutable std::size_t                  foldGutterCacheWindowEnd_         = 0;
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
    // main-editor-sticky-scroll follow-up: the raw sorted-by-startByte
    // marker list EnsureSymbolGutterCache below collapses down to one
    // {line, kind} entry per line -- kept here too, in full (absolute byte
    // coordinates, huge-window-remapped the same way), since sticky scroll's
    // StickyChainForViewportTop needs each marker's real endByte/name, which
    // the gutter's own collapsed shape throws away. EnsureSymbolGutterCache
    // calls EnsureSymbolMarkersCache first and derives its own cache from
    // this one, rather than the two independently calling mode_.symbolKind
    // and reparsing/re-querying twice per Paint().
    mutable text::Buffer*                     symbolMarkersCacheBuffer_            = nullptr;
    mutable std::size_t                       symbolMarkersCacheContentGeneration_ = 0;
    mutable std::size_t                       symbolMarkersCacheWindowStart_       = 0;
    mutable std::size_t                       symbolMarkersCacheWindowEnd_         = 0;
    mutable std::vector<editor::SymbolMarker> symbolMarkersCache_;
    void                                       EnsureSymbolMarkersCache() const;

    mutable text::Buffer*                                           symbolGutterCacheBuffer_            = nullptr;
    mutable std::size_t                                             symbolGutterCacheContentGeneration_ = 0;
    // huge-file-structural-gutters follow-up: see
    // foldableBlocksCacheWindowStart_/End_'s own doc comment above -- same
    // shape, for symbolGutterLineKinds_ instead of foldableBlocksCache_.
    mutable std::size_t                                             symbolGutterCacheWindowStart_       = 0;
    mutable std::size_t                                             symbolGutterCacheWindowEnd_         = 0;
    mutable std::vector<std::pair<std::size_t, editor::SymbolKind>> symbolGutterLineKinds_;

    // test-runner integration: per-line pass/fail/skip marks, the symbol
    // cache's shape with one extra generation stamp -- invalidated by a
    // content change (discovery re-runs) OR a fresh parsed outcome
    // (TestRunner::OutcomeGeneration()). Unlike symbolGutterLineKinds_ the
    // data is half external (the outcome) and half derived (discovery) --
    // both stamps together are what keep it honest. See
    // EnsureTestGutterCache in BufferView.cpp.
    mutable text::Buffer*                                                            testGutterCacheBuffer_            = nullptr;
    mutable std::size_t                                                              testGutterCacheContentGeneration_ = 0;
    mutable std::size_t                                                              testGutterCacheOutcomeGeneration_ = 0;
    // huge-file-structural-gutters follow-up: see
    // foldableBlocksCacheWindowStart_/End_'s own doc comment above -- same
    // shape, for testGutterLineStatuses_ instead of foldableBlocksCache_.
    mutable std::size_t                                                              testGutterCacheWindowStart_       = 0;
    mutable std::size_t                                                              testGutterCacheWindowEnd_         = 0;
    mutable std::vector<std::pair<std::size_t, editor::testrun::TestResult::Status>> testGutterLineStatuses_;

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
    // Hunk-navigation follow-up: one 0-indexed line per hunk -- its first
    // affected line for Added/Modified, the same boundary line a pure
    // deletion's own DiffLineKind::Removed entry already marks. Sorted
    // ascending (built alongside diffLineKinds_ in DispatchDiffForTesting,
    // from the same hunk list), so JumpToNextHunk/JumpToPreviousHunk are a
    // plain upper_bound/lower_bound away from "the next/previous hunk
    // relative to point's line" -- no need to re-derive hunk boundaries by
    // scanning diffLineKinds_' flattened per-line form.
    mutable std::vector<std::size_t> diffHunkStartLines_;
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

    // codeLens follow-up. AnnotationRowsForLine's leading (above-the-line)
    // sibling -- today only a trailing row exists (the diagnostic one
    // above); a code lens reads more naturally as a heading over the code
    // it describes, matching real editors' own convention for this
    // feature. Reads lspManager_->CodeLensSpans(buffer) fresh on each call
    // (a plain small-vector scan, cheap enough that no BufferView-side
    // cache/generation-tracking was worth adding -- see this method's own
    // definition comment for why that's a deliberate deviation from
    // AnnotationRowsForLine's own EnsureInlineDiagnosticCache precedent).
    // 0 while editor::lsp::LspCodeLensEnabled() is off or lspManager_ is
    // unset.
    [[nodiscard]] std::size_t LeadingAnnotationRowsForLine(std::size_t line) const;
    // Paints one leading row for `line` at screen row `row`: every lens
    // whose range starts on `line`, titles joined by " | ", dim italic
    // text -- no carets (unlike PaintInlineDiagnosticRow, a lens isn't
    // anchored to a sub-line span, just the line as a whole).
    void PaintCodeLensRow(Canvas& c, int row, std::size_t line, std::size_t gutterWidth) const;
    // main-editor-sticky-scroll follow-up: draws the pinned namespace/class/
    // method breadcrumb rows into the TOP of `c` (still at its full,
    // unshifted size when this runs) and returns how many rows it drew --
    // Paint() then reassigns its own local `c` to a Canvas::ForBox view
    // shifted down by that count, so the ordinary per-line rendering loop
    // that follows needs no changes of its own to make room. Requires
    // EnsureSymbolMarkersCache() to have already been called this frame.
    [[nodiscard]] int PaintStickyScrollRows(Canvas& c, std::size_t gutterWidth) const;
    // main-editor-sticky-scroll follow-up: the same capped
    // StickyChainForViewportTop query PaintStickyScrollRows renders,
    // factored out so OnEvent's click-to-jump handler resolves exactly the
    // same rows against exactly the same cap without recomputing the cap
    // logic a second, possibly-drifting way. Calls EnsureSymbolMarkersCache()
    // itself -- cheap/idempotent when already fresh -- rather than assuming
    // Paint() already ran this frame before a caller reaches this.
    [[nodiscard]] std::vector<editor::SymbolMarker> StickyScrollChainForCurrentViewport() const;

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
    // LspComplete doc comment and completionDebounceTimer_ above for the
    // request/debounce flow; ActiveCompletion itself is transient UI state,
    // not modal -- it coexists with ordinary InputMode::Normal editing
    // rather than replacing it (no dedicated InputMode value), the same way
    // e.g. a diagnostic gutter marker does. completion-popup follow-up:
    // renamed from GhostCompletion -- rendering moved from an inline dimmed
    // suffix to a real ListPopup (see NotifyCompletionChanged), so nothing
    // about this state is "ghost" anymore.
    struct ActiveCompletion {
        std::size_t                              requestPoint = 0; // buffer.Point() when this was requested/received -- stale if point has since moved
        std::vector<editor::lsp::CompletionItem> items;
        std::size_t                              selectedIndex = 0;
        // Self-hosting-completion follow-up: the word-boundary start each
        // item's insertText was ranked/computed against (WordPrefixStart's
        // ASCII alnum/'_' rule for LSP/dabbrev items, JanetSymbolPrefixStart's
        // wider '-'/'/' -inclusive rule for ned/* binding items) -- stored
        // once at request time rather than recomputed by CompletionInsertSuffix
        // from whichever rule happens to be lexically closest, since the two
        // rules disagree on where a name like "ned/register-command" starts.
        std::size_t prefixStart = 0;
    };
    std::optional<ActiveCompletion> activeCompletion_;

    // completion-popup follow-up: the screen-absolute anchor last sent to
    // onCompletionChanged_, so Paint() can cheaply detect "point's on-screen
    // position moved since the last notify" (a pure scroll, which touches no
    // ActiveCompletion field at all) and re-notify -- see
    // NotifyCompletionChanged's own doc comment.
    std::optional<Point> lastNotifiedCompletionAnchor_;

    // Debounce deadline for an automatic completion request -- set by
    // MaybeScheduleAutoCompletion, consumed once completionDebounceTimer_
    // below fires RequestCompletionAtPoint() and clears it. std::nullopt
    // means no request pending. Overwriting it on every qualifying keystroke
    // (rather than tracking multiple pending deadlines) is what makes this
    // act as a debounce, not a fixed-interval repeat -- more typing keeps
    // pushing the deadline out.
    std::optional<std::chrono::steady_clock::time_point> completionDebounceDeadline_;
    // The actual wakeup mechanism -- MaybeScheduleAutoCompletion arms this
    // (via EventLoop::Post's Arm) for exactly completionDebounceDeadline_'s
    // own remaining delay each time it moves the deadline out.
    // completionDebounceDeadline_ itself is kept anyway (rather than
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

    // documentHighlight follow-up. BufferView-owned, ephemeral point-
    // triggered UI state -- same lifecycle class as ActiveCompletion above,
    // not Buffer-owned server-pushed state like Diagnostics(). buffer/
    // contentGeneration guard Paint()'s own read (see
    // currentLineDocumentHighlightSpans) against a stale response landing
    // after the buffer switched or was edited; ranges are byte [start,end)
    // resolved against the buffer's content at the moment the response
    // arrived.
    struct DocumentHighlightState {
        text::Buffer* buffer            = nullptr;
        std::size_t   contentGeneration = 0;
        std::size_t   requestPoint      = 0;
        std::vector<std::pair<std::size_t, std::size_t>> ranges;
    };
    std::optional<DocumentHighlightState> documentHighlight_;
    // Same debounce shape as completionDebounceTimer_ above, entirely
    // independent of it -- armed by MaybeScheduleDocumentHighlight on any
    // point movement (not just self-insert keystrokes, since motion commands
    // must also refresh the highlight set).
    DeadlineTimer documentHighlightDebounceTimer_;
    // Bumped by RequestDocumentHighlightAtPoint before every request -- same
    // staleness-guard shape as completionRequestGeneration_.
    std::size_t documentHighlightRequestGeneration_ = 0;

    void RequestDocumentHighlightAtPoint();
    void MaybeScheduleDocumentHighlight(std::size_t pointBefore, std::size_t generationBefore);

    // Debugging wishlist (line-inspect follow-up): DocumentHighlightState's
    // own shape -- buffer/contentGeneration guard Paint()'s read against a
    // stale set landing after the buffer switched or was edited; line is the
    // 0-based line dap-line-inspect ran against, invalidated (see
    // RunCommandAndHandleOutcome) the moment point leaves it. ranges are
    // every candidate span it fanned out an evaluate request for, regardless
    // of that request's own success/failure -- the highlight shows what was
    // inspected, not what resolved.
    struct LineInspectState {
        text::Buffer* buffer            = nullptr;
        std::size_t   contentGeneration = 0;
        std::size_t   line              = 0;
        std::vector<std::pair<std::size_t, std::size_t>> ranges;
    };
    std::optional<LineInspectState> lineInspect_;
    void                            LineInspectAtPoint();

    // signature-help-auto-trigger follow-up. Same debounce shape as
    // completionDebounceTimer_ above, entirely independent of it -- both can
    // be armed simultaneously (ghost-text completion and signature help
    // render to disjoint UI surfaces, inline vs. echo area, so there's no
    // reason for one to suppress the other).
    DeadlineTimer signatureHelpDebounceTimer_;
    // Bumped by RequestSignatureHelpAtPoint before every request -- same
    // staleness-guard shape as completionRequestGeneration_.
    std::size_t signatureHelpRequestGeneration_ = 0;

    void MaybeScheduleSignatureHelp(const editor::KeyChord& chord, std::size_t generationBefore);
    void RequestSignatureHelpAtPoint();

    // on-type-formatting follow-up. No debounce timer -- unlike completion/
    // signature-help this only fires once per matching trigger keystroke,
    // not repeatedly while typing, so the request goes out immediately.
    // Still needs the same staleness guard those two use: the LSP round
    // trip can't complete synchronously within the keystroke's own
    // dispatch, so by the time the response arrives the buffer/point may
    // have moved on.
    std::size_t onTypeFormattingRequestGeneration_ = 0;

    void MaybeScheduleOnTypeFormatting(const editor::KeyChord& chord, std::size_t generationBefore);

    // lsp-format-on-save follow-up. Mirrors Commands.cpp's saveBufferBody
    // shape (format step, backup, Buffer::Save, autosave cleanup, status
    // message) but deferred behind an LSP round trip -- invoked only when
    // Commands.cpp's save-buffer/save-buffer-force set
    // context.deferSaveForLspFormat (see RunCommandAndHandleOutcome's own
    // early branch for that field). Takes no force parameter: by the time
    // deferSaveForLspFormat is set, save-buffer's own ExternallyModified/
    // conflict-marker checks (or save-buffer-force's deliberate absence of
    // them) already ran in Commands.cpp -- there is nothing left here for
    // that distinction to gate.
    void        RequestLspFormatThenSaveBuffer();
    std::size_t lspFormatOnSaveRequestGeneration_ = 0;

    void RequestCompletionAtPoint();
    // dabbrev-fallback follow-up: the "no running LSP client for this
    // buffer's language" half of RequestCompletionAtPoint -- scans the
    // buffer itself (Editor/DabbrevComplete.h) for candidates instead of
    // asking a server, populating activeCompletion_ synchronously (no
    // generation/staleness bookkeeping needed, unlike the async LSP path).
    void ApplyDabbrevCompletion(text::Buffer& buffer, std::size_t point);
    // Self-hosting-completion follow-up: the "Janet-mode buffer" half of
    // RequestCompletionAtPoint, tried ahead of ApplyDabbrevCompletion --
    // fuzzy-ranks every live "ned/*" binding name (Janet/Environment.h's
    // BindingNamesWithPrefix) against the Janet-symbol-aware prefix at point
    // (JanetSymbolPrefixStart, not WordPrefixStart -- see ActiveCompletion's
    // own prefixStart doc comment). Returns false (activeCompletion_ left
    // untouched) when there's no janetEnv_ wired, the prefix is empty, or
    // nothing fuzzy-matches, so the caller falls through to plain
    // dabbrev-expand instead of showing an empty suggestion.
    [[nodiscard]] bool        ApplyJanetBindingCompletion(text::Buffer& buffer, std::size_t point);
    [[nodiscard]] bool        ShouldSuppressAutoCompletion() const;
    void                      MaybeScheduleAutoCompletion(const editor::KeyChord& chord, std::size_t generationBefore);
    void                      AcceptActiveCompletion();
    void                      CycleActiveCompletion(int direction);
    [[nodiscard]] std::string CompletionInsertSuffix(const editor::lsp::CompletionItem& item) const;

    // completion-popup follow-up: builds a ListPopupModel from
    // activeCompletion_ (kind glyph + label + detail per row, anchored at
    // point's current on-screen position via CursorPosition()/Box_()) and
    // fires it through onCompletionChanged_, or fires std::nullopt when
    // activeCompletion_ is unset or point's row isn't currently on screen.
    // Called at every activeCompletion_ mutation site (mirrors
    // onCandidatesChanged_'s own many explicit call sites elsewhere in this
    // class) -- also called once per Paint() when activeCompletion_ is set
    // and the freshly computed anchor differs from
    // lastNotifiedCompletionAnchor_, which is what keeps the popup glued to
    // point across a pure scroll (no activeCompletion_ mutation site can
    // observe that on its own).
    void NotifyCompletionChanged();
    // The screen-absolute position NotifyCompletionChanged anchors the
    // popup to -- CursorPosition()'s local result plus Box_()'s own origin
    // (main.cpp's real-terminal-cursor conversion, reused here), one row
    // below point. std::nullopt when point's row isn't currently on screen
    // (CursorPosition() itself returns nullopt then).
    [[nodiscard]] std::optional<Point> CompletionAnchorNow() const;

    // code-actions follow-up: pendingCodeActions_/codeActionSelection_ are
    // valid only while inputMode_ is LspCodeActionSelect (see
    // RequestCodeActionsAtPoint's own doc comment above for why inputMode_
    // only ever changes from inside that async callback).
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

    // declaration/typeDefinition/implementation follow-up: the lowercase
    // human-facing word for whichever LspLocationKind pendingDefinitions_
    // was most recently requested for ("definition", "declaration", ...) --
    // stamped alongside pendingDefinitions_ inside RequestDefinitionAtPoint's
    // own async callback, so RefreshDefinitionSelectStatus's wording matches
    // the request that's actually pending rather than always saying
    // "Definition" regardless of kind.
    std::string pendingLocationLabel_ = "definition";

    // find-references follow-up: same staleness-guard shape as
    // definitionRequestGeneration_, kept separate (rather than sharing that
    // counter) since RequestProjectFindReferences's LSP path builds its own
    // multibuffer result directly, never entering LspGotoDefinitionSelect.
    std::size_t referencesRequestGeneration_ = 0;

    // symbol-search follow-up: documentSymbolCandidates_/documentSymbolLabels_
    // are parallel vectors (candidates_[i] is what labels_[i] describes) --
    // FuzzyFilterAndRank only takes/returns display strings, so a ranked
    // label is mapped back to its SymbolResult by exact string lookup
    // against documentSymbolLabels_ (BuildSymbolLabel folds in the line
    // number, which makes a label collision between two real, distinct
    // symbols vanishingly unlikely; the rare collision just means Enter
    // picks whichever of them comes first -- a harmless degrade, not a
    // crash). documentSymbolSelection_/documentSymbolRequestGeneration_
    // mirror definitionSelection_/definitionRequestGeneration_'s own shape.
    std::vector<editor::lsp::LspManager::SymbolResult> documentSymbolCandidates_;
    std::vector<std::string>                           documentSymbolLabels_;
    std::size_t                                        documentSymbolSelection_         = 0;
    std::size_t                                        documentSymbolRequestGeneration_ = 0;

    // symbol-search follow-up: workspace/symbol's own live-requery
    // counterpart -- pendingWorkspaceSymbols_/workspaceSymbolLabels_ hold
    // only the *current* response (replaced wholesale on every request,
    // unlike documentSymbolCandidates_'s one-shot fetch), already in
    // server-ranked order, so no local FuzzyFilterAndRank runs over them at
    // all -- HandleWorkspaceSymbolKey's Up/Down navigate this list directly.
    std::vector<editor::lsp::LspManager::SymbolResult> pendingWorkspaceSymbols_;
    std::vector<std::string>                           workspaceSymbolLabels_;
    std::size_t                                        workspaceSymbolSelection_         = 0;
    std::size_t                                        workspaceSymbolRequestGeneration_ = 0;
    // See completionDebounceTimer_'s own comment -- same DeadlineTimer-based
    // debounce shape, reusing LspCompletionDebounceMs() rather than adding a
    // second, parallel Janet setting for what's the same underlying need.
    DeadlineTimer workspaceSymbolDebounceTimer_;

    // header-source-switching follow-up: same staleness-guard shape as
    // definitionRequestGeneration_ above, no selection list needed --
    // switchSourceHeader never returns more than one candidate.
    std::size_t switchHeaderSourceRequestGeneration_ = 0;

    // rename follow-up: same staleness-guard shape once more. renameTitle_
    // is the human-readable "N edits across M files" summary shown in the
    // final "Renamed (...)" status message -- set right before ApplyRename
    // runs, applied with no separate confirmation step.
    std::string   renameTitle_;
    std::size_t   renameRequestGeneration_ = 0;
    // prepareRename follow-up: same staleness-guard shape once more, for the
    // request RequestPrepareRenameAtPoint sends before lsp-rename opens its
    // prompt.
    std::size_t prepareRenameRequestGeneration_ = 0;
    // linked-editing-range follow-up: same staleness-guard shape once more.
    std::size_t linkedEditingRequestGeneration_ = 0;

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
    // See completionDebounceTimer_'s own comment -- same DeadlineTimer-based
    // wakeup mechanism, this time backing statusMessageChangedAt_'s
    // idle-clear deadline.
    DeadlineTimer statusMessageTimer_;

    // Detects a statusMessage_ change since the last call (from Paint(),
    // which runs after every real render including every keystroke) and
    // (re)arms the idle-clear deadline via statusMessageTimer_, which fires
    // it once the deadline passes with nothing further changing it.
    void EnsureStatusMessageFreshness();

    // diagnostics-log-round-2 follow-up: sets statusMessage_ (the existing
    // echo-area report, unchanged) and mirrors the same text into the
    // durable *Messages* log (Editor/DiagnosticsLog.h) as an Error entry, so
    // a failure that used to only flash through the echo area and vanish
    // now leaves a record. category defaults to General for command/file-op
    // failures that aren't tied to one subsystem; call sites that know
    // better (LSP, VCS, ...) pass their own.
    void ReportError(std::string message, editor::LogCategory category = editor::LogCategory::General);
};

} // namespace ned::ui

#endif // NED_UI_BUFFERVIEW_H
