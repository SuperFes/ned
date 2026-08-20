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

namespace ned::editor {

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
                                ToggleProjectSidebar,
                                // org-agenda follow-up: another one-shot direct action, same
                                // shape as ToggleProjectSidebar -- BufferView builds and
                                // switches to a synthesized "*agenda*" buffer via
                                // editor::CollectProjectTodos + its own existing
                                // BuildResultsBuffer helper (already shared with
                                // project-search/project-replace).
                                ProjectAgenda,
                                CreateDirectory,
                                DeleteFile,
                                RenameFile,
                                FindScratch,
                                // org-set-tags follow-up: another prompt-shaped request (real
                                // Org's own C-c C-q) -- tags are free-form text, not a small
                                // fixed set to cycle through the way org-cycle-todo/
                                // org-cycle-priority do, so this needs a real prompt. See
                                // Editor/Org.h's SetHeadlineTags for the actual rewrite.
                                SetHeadlineTags,
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
                                // code-actions follow-up: a one-shot direct action, same shape
                                // as LspComplete -- BufferView's RequestCodeActionsAtPoint sends
                                // the actual textDocument/codeAction request and owns the
                                // resulting select/confirm InputMode session, entered only once
                                // the (async) response actually arrives -- see that method's own
                                // doc comment in BufferView.h.
                                LspCodeAction,
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
                                // Minimap widget follow-up: another one-shot direct action, same
                                // shape as ToggleProjectSidebar -- BufferView flips the registered
                                // Minimap's own `active` flag directly (and the paired ScrollBar
                                // column's opposite), no InputMode session needed.
                                ToggleMinimap };

// Everything a command implementation might need. Built fresh per invocation
// from live references -- never stored, so there's no lifetime concern beyond
// the synchronous call it's used in.
struct CommandContext {
    text::Buffer&      buffer;
    text::KillRing&    killRing;
    text::BufferList&  bufferList;
    KeyChord           triggeringKey{};              // the chord that completed this dispatch, if any
    std::string*       message            = nullptr; // where a command reports a status/echo-area message, if anywhere
    bool               quit               = false;   // set by a command requesting the application exit
    InteractiveRequest interactiveRequest = InteractiveRequest::None;
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
