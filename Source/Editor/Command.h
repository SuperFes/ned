//
// The "interactive defun" equivalent: every editor action is a named Command
// registered here, invocable either from a keybinding (via Dispatcher) or by
// name -- M-x-style, via BufferView's own fuzzy-filtered
// (Editor/FuzzyMatch.h) CommandRegistry::Invoke, see the
// execute-extended-command command in Commands.cpp and
// BufferView::HandleExecuteCommandKey. CompleteCommandNames below is a
// separate, simpler exact-prefix completion utility, not what M-x itself
// uses.
//

#ifndef NED_EDITOR_COMMAND_H
#define NED_EDITOR_COMMAND_H

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Key.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "Text/KillRing.h"

namespace ned::editor::lsp {
class LspManager;
} // namespace ned::editor::lsp

namespace ned::editor::tasks {
class TaskRunner;
} // namespace ned::editor::tasks

namespace ned::editor::testrun {
class TestRunner;
} // namespace ned::editor::testrun

namespace ned::editor {

class ProjectUndoManager;

struct Mode;

// Set by a command that needs to hand control to an interactive sub-session
// (isearch, query-replace, ...) the host UI drives -- the command layer only
// requests it, it doesn't know how to run one itself (that needs live key
// input, which is a UI concern). Mirrors how `quit`/`message` already let a
// command signal an editor-level intent back to whatever's hosting it.
enum class InteractiveRequest { None,
                                IsearchForward,
                                IsearchBackward,
                                QueryReplace,
                                ConfirmQuit,
                                FindFile,
                                SwitchToBuffer,
                                ProjectSearch,
                                VisitSearchResult,
                                ProjectReplace,
                                // find-all-references follow-up: one-shot direct action, same
                                // shape as VcsFullDiffBuffer/DiagnosticsBuffer -- switches to a
                                // synthesized, read-only "*references: <word>*" multibuffer
                                // (Editor/Multibuffer.h) built from a whole-word RE2 scan
                                // (Editor/ProjectSearch.h's now-internal SearchDirectory) for
                                // the identifier at point, one excerpt per matching line. A
                                // fast textual approximation, not real semantic LSP references
                                // (no textDocument/references support exists yet -- see
                                // ROADMAP.md's Multibuffers entry for why this was reframed to
                                // not wait on that). vcs-visit-result already jumps to source
                                // from any excerpt -- MultibufferIndexFor/VisitVcsResult are
                                // generic over which multibuffer consumer built the buffer, not
                                // VCS-specific despite the name.
                                ProjectFindReferences,
                                ToggleProjectSidebar,
                                // terminal-panel follow-up: one-shot direct action, same
                                // shape as ToggleProjectSidebar -- BufferView just forwards
                                // to the callback main.cpp registered (SetOnTerminalToggle),
                                // since the panel is an OverlayHost overlay owned above the
                                // window/pane level, not per-pane state.
                                ToggleTerminal,
                                // generic-popup follow-up: one-shot direct action, same shape
                                // as ToggleTerminal -- BufferView forwards to the registered
                                // callback (SetOnBufferListToggle), since the buffer-list panel
                                // is an OverlayHost overlay owned above the window/pane level,
                                // not per-pane state.
                                ListBuffers,
                                // sidebar-keyboard-focus follow-up: one-shot direct action,
                                // same shape as ToggleProjectSidebar -- BufferView expands
                                // the registered sidebar if collapsed and hands it the
                                // keyboard focus (ProjectSidebar's own key handling takes
                                // over until Enter/Escape/C-g returns focus).
                                FocusProjectSidebar,
                                // VCS side panel: same "just set
                                // interactiveRequest" shape as
                                // ToggleProjectSidebar/FocusProjectSidebar --
                                // BufferView keeps the panel and
                                // ProjectSidebar mutually exclusive on the
                                // left dock (see BufferView::SetVcsPanel's
                                // own doc comment).
                                ToggleVcsPanel,
                                FocusVcsPanel,
                                // org-agenda follow-up: another one-shot direct action, same
                                // shape as ToggleProjectSidebar -- BufferView builds and
                                // switches to a synthesized "*agenda*" buffer.
                                // scheduling/recurrence follow-up: now a real date-driven
                                // agenda via editor::CollectAgendaItems + BufferView's own
                                // BuildAgendaMultibuffer (a sectioned Editor/Multibuffer.h
                                // view, not the flat BuildResultsBuffer this used to build).
                                ProjectAgenda,
                                // diagnostics-log follow-up: another one-shot direct action,
                                // same shape as ProjectAgenda just above -- BufferView
                                // rebuilds and switches to the "*Messages*" buffer
                                // (Editor/DiagnosticsLog.h's RebuildMessagesBuffer).
                                ShowMessages,
                                CreateDirectory,
                                DeleteFile,
                                RenameFile,
                                FindScratch,
                                // backup-and-recovery follow-up: recover-file's pick-a-version
                                // prompt over the active buffer's backups/autosave -- see
                                // BufferView::HandleRecoverFileKey.
                                RecoverFile,
                                // org-set-tags follow-up: another prompt-shaped request (real
                                // Org's own C-c C-q) -- tags are free-form text, not a small
                                // fixed set to cycle through the way org-cycle-todo/
                                // org-cycle-priority do, so this needs a real prompt. See
                                // Editor/Org.h's SetHeadlineTags for the actual rewrite.
                                SetHeadlineTags,
                                // property-drawers follow-up: org-set-property (real
                                // Org's own C-c C-x p) needs two prompts in sequence
                                // (property name, then its value) -- a dedicated
                                // handler/InputMode, not this shared prompt-shaped
                                // enum (see BufferView::HandleSetPropertyKey).
                                // org-delete-property (one prompt, the property name)
                                // does fit here.
                                SetProperty,
                                DeleteProperty,
                                // scheduling/recurrence follow-up: org-schedule/org-deadline
                                // (real Org's own C-c C-s/C-c C-d) -- same one-prompt shape
                                // as SetHeadlineTags, pre-filled with the headline's current
                                // SCHEDULED:/DEADLINE: timestamp if it has one. See
                                // Editor/Org.h's SetPlanning for the actual rewrite.
                                OrgSchedule,
                                OrgDeadline,
                                // execute-extended-command follow-up: another prompt-shaped
                                // one-shot request, not a structural window-management one --
                                // placed here rather than after the window-management block
                                // below for that reason.
                                ExecuteCommand,
                                // project-find-file follow-up: another prompt-shaped one-shot
                                // request, same shape as ExecuteCommand (fuzzy-narrowed, live
                                // as-you-type) but over project files instead of command
                                // names -- see BufferView::HandleProjectFindFileKey.
                                ProjectFindFile,
                                // named-projects follow-up: another prompt-shaped one-shot
                                // request, same fuzzy-narrowed shape as ProjectFindFile but
                                // over registered project names/roots (Editor/ProjectRegistry.h)
                                // instead of files -- see BufferView::HandleSwitchProjectKey.
                                SwitchProject,
                                // named-projects follow-up: a genuinely different shape from
                                // SwitchProject/ProjectFindFile -- a plain path-entry prompt
                                // (FindFile's own shape, including its tab-completion), then,
                                // for a not-yet-registered root, a second plain prompt for a
                                // display name (BookmarkSet's own pre-filled-prompt shape) --
                                // see BufferView::HandleOpenProjectPathKey/
                                // HandleOpenProjectNameKey.
                                OpenProject,
                                // rich-theme-set follow-up (Phase 1): the select-theme picker,
                                // same fuzzy prompt-shaped session as ProjectFindFile just
                                // above but over theme names, with the highlighted candidate
                                // live-previewed before Enter commits -- see
                                // BufferView::HandleSelectThemeKey.
                                SelectTheme,
                                // theme-editing follow-up: one-shot direct action (no prompt) --
                                // writes the currently-active theme out as an editable
                                // theme.janet; see BufferView::StartInteractiveSession's case.
                                SaveTheme,
                                // kmacro-start-macro/kmacro-end-or-call-macro follow-up: also
                                // one-shot direct actions (BufferView::StartInteractiveSession
                                // acts on them immediately, inputMode_ stays Normal), not
                                // prompt-shaped sessions -- see Dispatcher::StartRecording/
                                // StopRecording/LastMacro for where the actual recording state
                                // lives.
                                StartKbdMacro,
                                EndOrCallKbdMacro,
                                // point-to-register/jump-to-register/copy-to-register/
                                // insert-register follow-up: also prompt-shaped one-shot
                                // requests -- each reads exactly one further character (the
                                // register name) and acts, no MinibufferPrompt needed. See
                                // Editor/Register.h for where the actual register storage
                                // lives.
                                PointToRegister,
                                JumpToRegister,
                                CopyToRegister,
                                InsertRegister,
                                // org-capture follow-up: same "read exactly one further
                                // character, no MinibufferPrompt" shape as the register
                                // requests above -- the character picks a registered
                                // Editor/OrgCapture.h template by key. See
                                // BufferView::HandleOrgCaptureKey.
                                OrgCapture,
                                // kill-rectangle/delete-rectangle/yank-rectangle/
                                // string-rectangle follow-up: KillRectangle/DeleteRectangle/
                                // YankRectangle are one-shot direct actions, no further
                                // prompting needed at all (unlike the register requests above,
                                // there's no name character to read) -- same shape as
                                // ToggleProjectSidebar. StringRectangle is the one prompt-shaped
                                // exception (needs one line of typed replacement text). See
                                // Editor/Rectangle.h for where the actual operations live.
                                KillRectangle,
                                DeleteRectangle,
                                YankRectangle,
                                StringRectangle,
                                // narrow-to-region/widen follow-up: also one-shot direct actions,
                                // same shape as ToggleProjectSidebar. See Buffer::NarrowToRegion/
                                // Widen for where the actual restriction lives.
                                NarrowToRegion,
                                Widen,
                                // Emacs-coverage follow-up: Recenter is a one-shot direct action
                                // (same shape as ToggleProjectSidebar) -- scrolling is
                                // BufferView's own topLine_, so the command can only request it.
                                // GotoLine is prompt-shaped (HandlePromptKey), the same
                                // single-line-of-text session CreateDirectory runs.
                                Recenter,
                                GotoLine,
                                // external-modification-safety follow-up: save-buffer found the
                                // file changed on disk underneath the buffer (supersession) and
                                // wants a y/n overwrite confirmation before writing anything --
                                // BufferView runs the session (mirroring ConfirmCloseBuffer's
                                // shape) and invokes "save-buffer-force" on y.
                                ConfirmOverwriteSave,
                                // external-modification-round-2 follow-up: save-buffer found
                                // unresolved "<<<<<<<" conflict markers still in the buffer
                                // (a pending AutoMerge conflict, or a hand-pasted one) and
                                // wants a y/n confirmation before writing them to disk --
                                // same shape as ConfirmOverwriteSave, "y" saves anyway.
                                ConfirmSaveWithConflicts,
                                // Window-splitting follow-up: structural window-management
                                // actions, not single-buffer interactive sessions -- BufferView
                                // just forwards these to whatever registered
                                // SetOnWindowRequest (see BufferView.h), the same "command
                                // signals intent, host UI acts on it" shape every other
                                // InteractiveRequest already uses.
                                SplitBelow,
                                SplitRight,
                                DeleteWindow,
                                DeleteOtherWindows,
                                OtherWindow,
                                // Split-resize follow-up: same "forward to WindowManager" shape
                                // as the five window-management values just above -- grows/shrinks
                                // the focused pane against its nearest matching-axis split
                                // ancestor by one step (repeatable via the ordinary prefix-argument
                                // mechanism, no special-casing needed here).
                                EnlargeWindow,
                                ShrinkWindow,
                                EnlargeWindowHorizontally,
                                ShrinkWindowHorizontally,
                                // Links follow-up: another one-shot direct action (same shape as
                                // VisitSearchResult/ToggleProjectSidebar) -- BufferView's own
                                // OpenLinkAtPoint does the actual detect-and-open, trying Org's
                                // [[target][description]] bracket syntax first in an org-mode
                                // buffer, falling back to Editor/Link.h's generic bare-URL/file
                                // detection everywhere else. See Editor/Org.h's LinkAtPoint and
                                // Editor/Link.h's DetectLinkAtPoint for where the actual detection
                                // lives.
                                OpenLinkAtPoint,
                                // kill-buffer follow-up: another one-shot direct action, same
                                // shape as ToggleProjectSidebar -- BufferView already has the
                                // real close-with-confirmation logic (RequestCloseBuffer/
                                // CloseBufferNow, driving InputMode::ConfirmCloseBuffer for a
                                // modified buffer), previously reachable only via TabBar's own
                                // close-icon click; this is the keyboard/M-x path onto that
                                // exact same logic, not new logic of its own.
                                KillBuffer,
                                // structural-selection-expansion follow-up: one-shot direct
                                // actions, same shape as ToggleProjectSidebar/NarrowToRegion --
                                // BufferView keeps its own expansion-history stack and calls
                                // the active Mode's expandSelection (Mode.h) directly, no
                                // InputMode session needed for either.
                                ExpandSelection,
                                ShrinkSelection,
                                // hover/completion follow-up: a one-shot direct action, same
                                // shape as ToggleProjectSidebar -- BufferView's
                                // RequestCompletionAtPoint sends the actual
                                // textDocument/completion request and owns the resulting ghost-
                                // text state; no InputMode session needed since ghost text
                                // coexists with ordinary Normal-mode editing rather than
                                // replacing it. lsp-hover has no InteractiveRequest of its own --
                                // see its Commands.cpp registration for why.
                                LspComplete,
                                // documentHighlight follow-up: a one-shot direct action, same
                                // shape as LspComplete above -- BufferView's manual
                                // lsp-document-highlight command routes here, but the actual
                                // live-on-cursor-move behavior is driven directly from
                                // RunCommandAndHandleOutcome's MaybeScheduleDocumentHighlight
                                // call, not through this InteractiveRequest at all; this exists
                                // only for M-x discoverability/explicit invocation.
                                LspDocumentHighlight,
                                // code-actions follow-up: a one-shot direct action, same shape
                                // as LspComplete -- BufferView's RequestCodeActionsAtPoint sends
                                // the actual textDocument/codeAction request and owns the
                                // resulting select/confirm InputMode session, entered only once
                                // the (async) response actually arrives -- see that method's own
                                // doc comment in BufferView.h.
                                LspCodeAction,
                                // quick-fix follow-up: LspCodeAction's no-ceremony sibling --
                                // BufferView::RequestQuickFixAtPoint sends the same request but
                                // applies the response's single unambiguous fix immediately
                                // (undo is the safety net), falling back to LspCodeAction's own
                                // select session only when the choice is genuinely ambiguous.
                                LspQuickFix,
                                // codeLens follow-up: same one-shot shape as LspQuickFix just
                                // above -- BufferView::RequestCodeLensAtPoint runs the first
                                // code lens on point's own line (resolving it first via
                                // codeLens/resolve if needed), no selection session at all
                                // (see that method's own doc comment for the deliberate v1
                                // "only the first lens" simplification).
                                LspRunCodeLensAtPoint,
                                // error-visibility follow-up: another one-shot direct action,
                                // same shape as ProjectAgenda -- BufferView finds-or-creates
                                // the shared *lsp log* buffer (lsp::kLspLogBufferName) and
                                // switches to it. Must go through InteractiveRequest rather
                                // than acting directly in the command function, same reason
                                // ProjectAgenda does: switching this pane's own active buffer
                                // needs activeBuffer_, which only BufferView has.
                                LspShowLog,
                                // go-to-definition/rename follow-up: two more one-shot direct
                                // actions, same shape as LspCodeAction -- BufferView's
                                // RequestDefinitionAtPoint/RequestRenameAtPoint send the actual
                                // textDocument/definition and textDocument/rename requests and
                                // own the resulting select/confirm sessions, entered only once
                                // each (async) response actually arrives. LspRename does open
                                // with a synchronous "new name" prompt first (HandlePromptKey's
                                // LspRenameNewName case) -- the request itself isn't sent until
                                // that's confirmed with Enter.
                                LspGotoDefinition,
                                // declaration/typeDefinition/implementation follow-up: three
                                // more one-shot direct actions, identical in shape to
                                // LspGotoDefinition -- BufferView::RequestDefinitionAtPoint
                                // takes an LspLocationKind parameter and drives all four
                                // through the same request/select/jump machinery.
                                LspGotoDeclaration,
                                LspGotoTypeDefinition,
                                LspGotoImplementation,
                                // symbol-search follow-up: LspGotoSymbol is one more async
                                // request/own-session action, same "async request, own
                                // response" shape as LspGotoDefinition above --
                                // BufferView::RequestDocumentSymbolsAtPoint sends
                                // textDocument/documentSymbol and, once the response arrives,
                                // opens a fuzzy-filtered picker over the results (mirrors
                                // ProjectFindFile's own picker shape, not the numbered
                                // LspGotoDefinitionSelect list -- a buffer can have many
                                // symbols, fuzzy-narrowing by typing is the better fit).
                                // LspWorkspaceSymbol differs from every other Lsp* entry here:
                                // its session is genuinely still async *after* it opens --
                                // each keystroke re-sends workspace/symbol (debounced) since
                                // the server does its own query-matching server-side, unlike
                                // a local fuzzy filter over an already-fetched list.
                                LspGotoSymbol,
                                LspWorkspaceSymbol,
                                // call/type-hierarchy follow-up: four more one-shot direct
                                // actions, same "async request, own session" shape as
                                // LspGotoSymbol above -- BufferView::RequestHierarchyAtPoint
                                // takes a direction parameter and drives all four through the
                                // same prepare/auto-expand-root/browse machinery. Unlike every
                                // other Lsp* session here, the resulting browse session hands
                                // real keyboard focus to a dedicated ui::TreeView overlay
                                // (Source/UI/TreeView.h) rather than staying in one of
                                // BufferView's own InputMode key handlers -- a hierarchy is
                                // genuinely tree-shaped and expands on demand, unlike every
                                // other LSP result view in this codebase (a flat list), so it
                                // needed a real widget instead of an echo-area numbered list.
                                LspCallHierarchyIncoming,
                                LspCallHierarchyOutgoing,
                                LspTypeHierarchySupertypes,
                                LspTypeHierarchySubtypes,
                                LspRename,
                                // task-runner follow-up: prompt-shaped requests, same "New
                                // name" -> HandlePromptKey shape LspRename established above --
                                // RunTask prompts for a task name and, on Enter, calls
                                // TaskRunner::RunTask and switches to the resulting
                                // "*task: <name>*" buffer; CancelTask prompts for a (presumably
                                // running) task name and calls TaskRunner::CancelTask. See
                                // Editor/Tasks/TaskRunner.h for the actual spawn/stream/cancel
                                // logic.
                                RunTask,
                                CancelTask,
                                // DAP client slice 1: four one-shot direct actions, same shape
                                // as ToggleProjectSidebar -- BufferView holds the shared
                                // DapManager (SetDapManager, mirroring SetTaskRunner) and acts
                                // immediately: DapContinue starts a session for the active
                                // mode's language or resumes a stopped one (F5), DapStop tears
                                // it down (S-F5), DapPause requests a stop, and
                                // DapToggleBreakpoint toggles a breakpoint on point's own line
                                // (F9). No InputMode sessions -- every prompt-shaped decision
                                // (which adapter, what launch config) is init.janet
                                // configuration, not an interactive question.
                                DapContinue,
                                DapStop,
                                DapPause,
                                DapToggleBreakpoint,
                                // DAP client slices 2/3: stepping (three more one-shot direct
                                // actions, F10/F11/S-F11), the *debug* stack/variables buffer
                                // and its per-line drill-in (both one-shot -- DapShowDebug
                                // chains the async stackTrace/scopes/variables requests and
                                // switches to the synthesized buffer when they land;
                                // DapExpandVariable expands the composite variable on point's
                                // own *debug* line in place), and DapEvaluate -- the one
                                // prompt-shaped request of the family (HandlePromptKey collects
                                // the expression, Enter sends the DAP evaluate request).
                                DapStepOver,
                                DapStepInto,
                                DapStepOut,
                                DapShowDebug,
                                DapExpandVariable,
                                DapEvaluate,
                                // DAP round 2: conditional/logpoint breakpoints
                                // (DapSetBreakpointCondition/DapSetBreakpointLogMessage,
                                // prompt-shaped like DapEvaluate, operating on point's
                                // own line like DapToggleBreakpoint), watch expressions
                                // (DapAddWatch prompts; DapRemoveWatch operates on
                                // point's own *debug* buffer line), the thread picker
                                // (DapSelectThread, numbered-choice like
                                // LspCodeActionSelect), editable *debug* buffer
                                // variables (DapSetVariable, prompt-shaped, operating on
                                // point's own *debug* buffer line), and the debug
                                // console panel toggle (DapToggleConsole, one-shot
                                // forward like AcpTogglePanel/ToggleTerminal).
                                DapSetBreakpointCondition,
                                DapSetBreakpointLogMessage,
                                DapAddWatch,
                                DapRemoveWatch,
                                DapSelectThread,
                                DapSetVariable,
                                DapToggleConsole,
                                // DAP round 3: hit-count breakpoints
                                // (DapSetBreakpointHitCondition, DapSetBreakpointCondition's
                                // exact sibling), function breakpoints
                                // (DapToggleFunctionBreakpoint, prompt-shaped, not tied to
                                // point's line), exception breakpoints
                                // (DapSelectExceptionBreakpoints, numbered-checklist like
                                // DapSelectThread but multi-select/toggle), and attach mode
                                // (DapAttach, one-shot like DapContinue but start-only --
                                // ned/set-dap-attach configuration, not a prompt).
                                DapSetBreakpointHitCondition,
                                DapToggleFunctionBreakpoint,
                                DapSelectExceptionBreakpoints,
                                DapAttach,
                                // VCS blame gutter follow-up: one-shot direct actions, same
                                // shape as ProjectAgenda/LspShowLog. VcsShowBlame is the
                                // primary, inline-in-place one: populates the gutter for the
                                // *current* buffer (BufferView::RequestBlameForCurrentBuffer)
                                // without switching away from it -- revised after the original
                                // v1 default (jumping straight to a separate *vcs blame*
                                // buffer) was tried and reported back as disconnected from the
                                // code actually being read, not useful as the default action.
                                // VcsBlameDetailAtPoint reads already-loaded gutter data (no
                                // new request) and reports the full author/date/summary for
                                // point's line via statusMessage_ -- the "on request" detail
                                // view the gutter's own fixed-width short-hash column can't fit.
                                // VcsBlameBuffer/VcsShowLog are the full-history views, each
                                // switching to a synthesized "*vcs blame <file>*"/"*vcs log
                                // <file>*" buffer once the (async) result arrives -- still
                                // available, just no longer what a bare "show blame" reaches
                                // for by default. See Editor/Vcs/VcsRunner.h for the actual
                                // spawn/parse logic behind all of these.
                                VcsShowBlame,
                                VcsBlameDetailAtPoint,
                                VcsBlameBuffer,
                                VcsShowLog,
                                // Same shape as VisitSearchResult -- parses a "path:line:"
                                // prefix off the current line (shared via BufferView's private
                                // JumpToPathLine helper) and jumps there; a silent no-op on a
                                // non-matching line (e.g. a *vcs log* buffer, which has no
                                // per-line source location) or one that doesn't match.
                                VisitVcsResult,
                                // VCS vocabulary-completion follow-up. VcsStatus/VcsBranches
                                // are one-shot direct actions switching to a synthesized
                                // "*vcs status*"/"*vcs branches*" buffer once the async result
                                // arrives (VcsShowLog's exact shape). VcsStageFile/
                                // VcsUnstageFile are one-shot too, acting on the *vcs status*
                                // buffer's line at point when that's the active buffer, else
                                // on the active buffer's own file (BufferView::
                                // ResolveVcsFileTarget). VcsSwitchBranch/VcsCreateBranch
                                // are prompt-shaped (HandlePromptKey): switch-branch
                                // fetches the branch list first so Tab completes against
                                // real branch names; create-branch is a plain name prompt.
                                // multi-line-commit-message follow-up: VcsCommit is no
                                // longer prompt-shaped -- it's a one-shot direct action
                                // that opens/switches to the *vcs commit message* buffer
                                // (BufferView::BeginVcsCommitMessage), a real,
                                // multi-line-editable buffer rather than MinibufferPrompt
                                // (which is single-line by construction). VcsCommitFinish
                                // (bound C-c C-c) and VcsCommitAbort (bound C-c C-k) are
                                // that buffer's own Mode-local keymap, wired only while
                                // it's the active buffer (see Commands.cpp's
                                // RegisterBuiltinCommands and Editor/Vcs/VcsRunner.h's
                                // kVcsCommitMessageFilename).
                                VcsStatus,
                                VcsStageFile,
                                VcsUnstageFile,
                                VcsCommit,
                                VcsCommitFinish,
                                VcsCommitAbort,
                                VcsBranches,
                                VcsSwitchBranch,
                                VcsCreateBranch,
                                // Hunk-staging follow-up: one-shot direct actions on the hunk
                                // covering point's line in a *source* buffer (not the status
                                // buffer -- a hunk needs a real source line). BufferView gates
                                // both on the buffer being unmodified: the diff reflects the
                                // file on disk while point reflects the buffer, so staging
                                // from mismatched line numbers would pick the wrong hunk.
                                VcsStageHunk,
                                VcsUnstageHunk,
                                // Hunk-navigation follow-up: one-shot direct point motion to
                                // the next/previous changed hunk in the *current* buffer
                                // (gitsigns' ]c/[c convention) -- reads BufferView's own
                                // diffHunkStartLines_ cache directly, no VcsRunner round trip,
                                // so (unlike VcsStageHunk/VcsUnstageHunk above) this never gates
                                // on the buffer being saved/unmodified.
                                VcsNextHunk,
                                VcsPreviousHunk,
                                // Multibuffers follow-up: one-shot direct action, same shape as
                                // VcsBlameBuffer/VcsShowLog -- switches to a synthesized,
                                // read-only "*vcs diff*" buffer stitching every changed file's
                                // real diff hunks together (Editor/Multibuffer.h), once the
                                // async VcsRunner::RequestFullDiff result arrives. Unlike those,
                                // its buffer also carries a MultibufferIndex, so
                                // vcs-visit-result (VisitVcsResult) jumps to source from
                                // anywhere inside an excerpt's body, not just a single
                                // "path:line:" index line.
                                VcsFullDiffBuffer,
                                // Diagnostics-multibuffer follow-up: same shape as
                                // VcsFullDiffBuffer -- switches to a synthesized, read-only
                                // "*diagnostics*" buffer stitching every open buffer's
                                // Code-origin LSP diagnostics together (one composite source
                                // line per diagnostic), built entirely synchronously
                                // (BufferList::Buffers() is already in memory, no VcsRunner-
                                // style async round trip needed). Its buffer carries a
                                // MultibufferIndex like VcsFullDiffBuffer's does, so
                                // vcs-visit-result jumps to source from any excerpt, and it
                                // also carries real Buffer::Diagnostic entries translated into
                                // composite byte space, so the ordinary diagnostic gutter/
                                // underline/severity-color/inline-annotation pipeline lights up
                                // unmodified -- no new rendering path.
                                DiagnosticsBuffer,
                                // Minimap widget follow-up: another one-shot direct action, same
                                // shape as ToggleProjectSidebar -- BufferView flips the registered
                                // Minimap's own `active` flag directly (and the paired ScrollBar
                                // column's opposite), no InputMode session needed.
                                ToggleMinimap,
                                // Tab-cycling follow-up: two more one-shot direct actions, same
                                // shape as KillBuffer -- switching this pane's own active buffer
                                // needs activeBuffer_, which only BufferView has (LspShowLog's
                                // exact reasoning). Next/previous in Buffers() (tab bar) order,
                                // wrapping at either end.
                                TabNext,
                                TabPrevious,
                                // prefix-argument follow-up: a genuine multi-keystroke reading
                                // session (same shape as IsearchForward/IsearchBackward, not a
                                // one-shot direct action) -- BufferView drives
                                // Editor/PrefixArgument.h's PrefixArgumentReader until a
                                // terminating (non-C-u/digit/"-") key arrives, then re-dispatches
                                // that key normally with the resolved value applied. See
                                // Dispatcher::Feed for where the resolved value actually acts.
                                UniversalArgument,
                                // Emacs-keymap-round-2 follow-up: zap-to-char (M-z) reads exactly
                                // one further character (the target to kill up to) -- same
                                // prompt-shaped, no-MinibufferPrompt shape as PointToRegister/etc.
                                // above, driven by BufferView::HandleZapToCharKey. See
                                // CommandContext::zapToCharAppend below for how the kill-append
                                // decision crosses from this command's own invocation (which has
                                // real access to lastCommand) to that later keystroke (which
                                // doesn't, since it never goes through Dispatcher::Feed).
                                ZapToChar,
                                // ACP client slice 2: three prompt/one-shot requests, same "just
                                // set interactiveRequest" shape as run-task/cancel-task/DapContinue
                                // above -- BufferView holds the shared AcpManager (SetAcpManager,
                                // mirroring SetDapManager) and does the actual work.
                                // AcpStartSession/AcpSendPrompt are prompt-shaped (HandlePromptKey
                                // collects an agent name / message text); AcpStopSession is a
                                // one-shot direct action. A session/request_permission prompt is
                                // never reached through this enum at all -- it's agent-initiated,
                                // not user-command-initiated, so BufferView::ShowAcpPermissionPrompt
                                // is called directly by WindowManager's AcpManager wiring instead
                                // (JumpToPathLine's own precedent for an externally-triggered
                                // entry point). See Editor/Acp/AcpManager.h.
                                AcpStartSession,
                                AcpSendPrompt,
                                AcpStopSession,
                                // ACP chat panel follow-up: one-shot direct action, same shape
                                // as ToggleTerminal above -- BufferView just forwards to the
                                // callback main.cpp registered (SetOnAcpPanelToggle), since the
                                // panel is an OverlayHost overlay owned above the window/pane
                                // level, not per-pane state.
                                AcpTogglePanel,
                                // header-source-switching follow-up: one-shot direct action, same
                                // "async request, own response" shape as LspGotoDefinition above --
                                // BufferView::SwitchHeaderSource tries clangd's
                                // textDocument/switchSourceHeader extension first (when an LSP
                                // client is running for the buffer's language), falling back to
                                // Editor/HeaderSource.h's same-basename/sibling-directory heuristic
                                // when the server has none, returns none, or no client is running
                                // at all -- unlike LspGotoDefinition, LSP absence here is a normal
                                // fallback path, not an error.
                                SwitchHeaderSource,
                                // jump-back-stack follow-up: one-shot direct action, same shape as
                                // SwitchHeaderSource above -- BufferView::JumpBack pops its own
                                // private per-pane jumpBackStack_ (pushed to by
                                // JumpToDefinition/goto-line/bookmark-jump/jump-to-register) and
                                // restores the saved (buffer, point).
                                JumpBack,
                                // jump-back-stack follow-up, forward direction: the redo
                                // counterpart to JumpBack above -- BufferView::JumpForward pops
                                // its own private per-pane jumpForwardStack_ (pushed to by
                                // JumpBack every time it actually jumps somewhere, and cleared
                                // by PushJumpMark, mirroring a browser's back/forward history:
                                // a fresh jump discards stale forward entries rather than
                                // leaving them reachable).
                                JumpForward,
                                // org-clock-display follow-up: another one-shot direct action,
                                // same shape as ProjectAgenda -- BufferView builds and switches
                                // to a synthesized "*clock report*" buffer (a
                                // Editor/Multibuffer.h view, BuildAgendaMultibuffer's own
                                // shape) listing every headline in the *current* buffer with a
                                // nonzero clocked total, own time plus a subtree rollup via
                                // org::TotalClockedMinutesForSubtree. Scoped to the active
                                // buffer only, not project-wide -- clocking commands
                                // (org-clock-in/-out) are themselves buffer-scoped, see
                                // Editor/Org.h's own top comment, item 8.
                                OrgClockReport,
                                // snippet-expansion follow-up: the requesting command found a
                                // registered snippet trigger (or a snippet-format completion) and
                                // filled CommandContext::snippetExpansion below; BufferView
                                // performs the actual expansion (delete the trigger, insert the
                                // parsed skeleton, start the tabstop session -- see
                                // Editor/Snippet.h) since a live session is driven key-by-key
                                // through its InputMode machinery, the same seam every other
                                // session-shaped request here uses.
                                SnippetExpand,
                                // test-runner integration: one-shot direct actions (no prompt --
                                // there's one project-wide test command, nothing to ask, unlike
                                // RunTask's name prompt above). RunTests spawns
                                // TestRunner::RunAll and switches to "*test output*";
                                // CancelTests kills the in-flight run; ShowTestResults builds/
                                // refreshes and switches to the parsed "*test results*" failures
                                // buffer; RunTestAtPoint resolves the innermost discovered test
                                // definition containing point (Mode::testDiscovery) and runs it
                                // through the configured filter template; RerunFailedTests
                                // re-runs every currently-failed result the same filtered way.
                                // See Editor/TestRun/TestRunner.h.
                                RunTests,
                                CancelTests,
                                ShowTestResults,
                                RunTestAtPoint,
                                RerunFailedTests,
                                // editor-ergonomics follow-up: find-recent-file, same
                                // "just signal intent" shape as ProjectFindFile -- the
                                // candidate list (Editor/RecentFiles.h's most-recent-first
                                // path list) and fuzzy-narrow/open-on-Enter logic both live
                                // in BufferView.
                                FindRecentFile,
                                // editor-ergonomics follow-up: bookmark-set is a single
                                // plain-text prompt (the bookmark's name, pre-filled with
                                // the buffer's own filename), fitting the shared
                                // HandlePromptKey else-chain FindScratch/TaskName use.
                                // Command.cpp checks the buffer has a path before setting
                                // this (Buffer::Path()) -- same "guard in the command body,
                                // not the InteractiveRequest handler" precedent org-set-tags
                                // established for HeadlineAtPoint.
                                BookmarkSet,
                                // BookmarkJump/BookmarkDelete are two distinct
                                // InteractiveRequests resolving to the *same* InputMode
                                // (RunTask/CancelTask's own precedent for TaskName) --
                                // both open the same fuzzy-narrowed bookmark-name picker,
                                // distinguished by BufferView's own bookmarkPromptAction_,
                                // set alongside inputMode_ in StartInteractiveSession.
                                BookmarkJump,
                                BookmarkDelete,
                                // next-error follow-up: two one-shot direct actions, same
                                // point-motion shape as VcsNextHunk/VcsPreviousHunk above, but
                                // walking whichever results buffer (Editor/NextError.h's
                                // "last results buffer") was most recently built instead of a
                                // per-buffer diff-hunk cache -- BufferView::NextError/
                                // PreviousError do the actual walk+jump.
                                NextError,
                                PreviousError,
                                // ACP checkpoint/rewind follow-up: one-shot direct action, same
                                // "just forward, the target lives above this class" shape as
                                // AcpTogglePanel above -- BufferView forwards to
                                // SetOnAcpRewindRequest (main.cpp wires it to show/focus the ACP
                                // panel and open its rewind picker; AcpPanel::OnEvent then reads
                                // AcpManager::CheckpointCount()/CheckpointAt() to render the list
                                // and calls AcpManager::RewindTo() on a digit keystroke -- see
                                // Editor/Acp/AcpManager.h for the checkpoint/rewind data model).
                                AcpRewind };

// Everything a command implementation might need. Built fresh per invocation
// from live references -- never stored, so there's no lifetime concern beyond
// the synchronous call it's used in.
struct CommandContext {
    text::Buffer&     buffer;
    text::KillRing&   killRing;
    text::BufferList& bufferList;
    KeyChord          triggeringKey{};   // the chord that completed this dispatch, if any
    std::string*      message = nullptr; // where a command reports a status/echo-area message, if anywhere
    // Emacs' `last-command`: the name of the command the Dispatcher invoked
    // immediately before this one (empty on the first dispatch, or when
    // invoked outside Dispatcher::Feed -- see LastInvokedCommand's own doc
    // comment in Dispatcher.h). Only yank-pop reads this today. Placed after
    // `message` so the existing positional aggregate-init call sites
    // (BufferView::MakeContext, tests) stay valid.
    std::string        lastCommand;
    bool               quit               = false; // set by a command requesting the application exit
    InteractiveRequest interactiveRequest = InteractiveRequest::None;
    // prefix-argument follow-up: the resolved C-u value applying to the
    // *next* command's dispatch, set by the host UI before that dispatch
    // (see Editor/PrefixArgument.h). Dispatcher::Feed is authoritative on
    // consuming/clearing this -- it applies a repeat count (and, for a small
    // hand-curated set of direction-symmetric motion commands, a direction
    // flip on a negative value) and resets it back to nullopt once the
    // resolved command has actually run, or when a key goes unbound.
    // Untouched by commands invoked outside Feed (M-x's own Registry().Invoke
    // path) -- a deliberate cut, same precedent as lastCommand above not
    // updating there either.
    std::optional<long> prefixArg;
    // multi-cursor-round-2 follow-up: set by add-cursor-below/-above and
    // select-next-occurrence right after AddCursorAt, to the offset they
    // just added -- an outbound field the same shape as message, letting
    // BufferView scroll to show the newly added cursor instead of the
    // (unmoved) primary point. select-all-occurrences deliberately leaves
    // this unset (no single natural target when many cursors are added at
    // once); ordinary motion/editing commands never touch it, so it stays
    // nullopt and BufferView's normal ScrollToShowPoint() runs unchanged.
    std::optional<std::size_t> newlyAddedCursorPoint;
    // lsp-format-on-save follow-up: set by save-buffer/save-buffer-force
    // when LSP-format-on-save is enabled, no external FormatCommand() takes
    // precedence, and a language server is actually running for this
    // buffer -- an outbound field the same shape as newlyAddedCursorPoint,
    // signaling BufferView::RunCommandAndHandleOutcome to hand the rest of
    // this save off to RequestLspFormatThenSaveBuffer(force) asynchronously
    // instead of running the normal synchronous post-command refresh, since
    // the actual save hasn't happened yet (an LSP round trip is pending).
    // The bool value is force, mirroring save-buffer-force's own meaning:
    // skip the ExternallyModified/conflict-marker guards (already checked,
    // for save-buffer, before this was set).
    std::optional<bool> deferSaveForLspFormat;
    // Emacs-keymap-round-2 follow-up (kill-append): set by the zap-to-char
    // command to whether the kill it's about to request (on the character
    // keystroke that follows, once InteractiveRequest::ZapToChar's session
    // is under way) should append to the current kill-ring entry --
    // decided here, inside zap-to-char's own invocation, since that's the
    // one point with real access to lastCommand; the later keystroke that
    // actually performs the kill bypasses Dispatcher::Feed entirely (same
    // shape as the register commands) and so never sees a meaningful
    // lastCommand of its own. BufferView::RunCommandAndHandleOutcome reads
    // this immediately after invoking zap-to-char and stashes it for that
    // later keystroke to consume.
    bool zapToCharAppend = false;
    // Rows currently visible in the buffer view, set by the host UI before
    // each dispatch (0 if unknown/headless) -- scroll-page-up/-down are the
    // only commands that read this; everything else ignores it.
    std::size_t viewportHeight = 0;
    // generic-code-folding follow-up: the active buffer's Mode, set by the
    // host UI before each dispatch (nullptr if unknown/headless) -- mirrors
    // viewportHeight's own "a UI fact a command needs" shape. Only
    // code-fold-toggle reads this; everything else ignores it. A command
    // context is never stored past the synchronous call it's used in (see
    // this struct's own doc comment above), so a raw, non-owning pointer is
    // fine here the same way it already is for `message` below.
    // code-fold-toggle/toggle-line-comment/forward-sexp/backward-sexp read
    // this; everything else ignores it.
    const Mode* mode = nullptr;
    // hover/completion follow-up: the editor-wide LspManager, set by the
    // host UI before each dispatch (nullptr if unset, e.g. headless tests)
    // -- same "a UI fact/resource a command needs" shape as mode above. Only
    // lsp-hover/lsp-complete read this. A raw, non-owning pointer is fine
    // here the same way it already is for message/mode -- lsp-hover does
    // hand its RequestHover callback a captured std::string* (message) that
    // outlives this synchronous call, but that's the same accepted "the
    // owning BufferView outlives the async response" lifetime this
    // subsystem's diagnostics-publish handler and the scratch-autosave
    // thread already rely on, not a new risk class this field introduces.
    lsp::LspManager* lspManager = nullptr;
    // task-runner follow-up: the editor-wide TaskRunner, set by the host UI
    // before each dispatch (nullptr if unset, e.g. headless tests) -- same
    // "a UI fact/resource a command needs" shape as lspManager above. Only
    // run-task/cancel-task read this.
    tasks::TaskRunner* taskRunner = nullptr;
    // project-undo follow-up: the editor-wide ProjectUndoManager, set by the
    // host UI before each dispatch (nullptr if unset, e.g. headless tests)
    // -- same "a UI fact/resource a command needs" shape as lspManager/
    // taskRunner above. Only undo/redo read this, to fold a multi-file LSP
    // edit's sibling files into a plain undo/redo invocation when the
    // current buffer sits exactly on that transaction's edge.
    ProjectUndoManager* projectUndo = nullptr;
    // snippet-expansion follow-up: outbound, paired with
    // InteractiveRequest::SnippetExpand -- what to replace with which
    // registered snippet body. [replaceStart, replaceEnd) is the trigger
    // word (or a completion's typed prefix); body is the raw snippet
    // syntax, parsed by BufferView at expansion time (Editor/Snippet.h).
    // At the end of this struct per its own doc comment above -- existing
    // positional aggregate-init call sites stay valid.
    struct SnippetExpansionRequest {
        std::size_t replaceStart;
        std::size_t replaceEnd;
        std::string body;
    };
    std::optional<SnippetExpansionRequest> snippetExpansion;
    // test-runner integration: the editor-wide TestRunner, set by the host
    // UI before each dispatch (nullptr if unset, e.g. headless tests) --
    // taskRunner's exact shape. Only the run-tests family reads this.
    // Appended at the end per this struct's positional-aggregate-init rule.
    testrun::TestRunner* testRunner = nullptr;
};

using CommandFunction = std::function<void(CommandContext&)>;

class Command {
  public:
    Command(std::string name, std::string docstring, CommandFunction function);

    [[nodiscard]] const std::string& Name() const;
    [[nodiscard]] const std::string& Docstring() const;
    void                             Invoke(CommandContext& context) const;

  private:
    std::string     Name_;
    std::string     Docstring_;
    CommandFunction Function_;
};

// Re-registering an existing name overwrites it: Janet redefining a command
// by reloading code is expected normal use, not an error to guard against.
class CommandRegistry {
  public:
    void Register(std::string name, std::string docstring, CommandFunction function);

    [[nodiscard]] const Command* Find(const std::string& name) const;

    // Throws std::out_of_range if name isn't registered.
    void Invoke(const std::string& name, CommandContext& context) const;

    [[nodiscard]] std::vector<std::string> Names() const; // sorted

  private:
    std::unordered_map<std::string, Command> commands_;
};

// M-x-style prefix completion over registered command names, sorted.
[[nodiscard]] std::vector<std::string> CompleteCommandNames(const CommandRegistry& registry, std::string_view prefix);

} // namespace ned::editor

#endif // NED_EDITOR_COMMAND_H
