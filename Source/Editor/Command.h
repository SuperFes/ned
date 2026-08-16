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

namespace ned::editor {

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
                                CreateDirectory,
                                DeleteFile,
                                RenameFile,
                                FindScratch,
                                // execute-extended-command follow-up: another prompt-shaped
                                // one-shot request, not a structural window-management one --
                                // placed here rather than after the window-management block
                                // below for that reason.
                                ExecuteCommand,
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
                                OtherWindow };

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
