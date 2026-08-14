//
// Bridges any foreign scripting language's context-free callback model
// (Janet today, potentially others later) to our explicitly-passed
// CommandContext/CommandRegistry/Keymap. Deliberately lives here, not under
// Source/Janet/, so a future second scripting backend can reuse it unmodified
// and share the same registry/keymap rather than fragmenting editor state.
//
// This is the one deliberate piece of global-ish state in the codebase: it
// exists only because a native callback (JanetCFunction, or an equivalent for
// another backend) has no way to receive extra context through its fixed
// signature. Everything else keeps passing CommandContext explicitly.
//

#ifndef NED_EDITOR_SCRIPTINGSESSION_H
#define NED_EDITOR_SCRIPTINGSESSION_H

#include "Command.h"
#include "Keymap.h"

namespace ned::editor {

struct ScriptingSession {
    CommandRegistry& registry;
    Keymap&          scriptKeymap; // where a backend's define-key equivalent writes bindings
    CommandContext*  context = nullptr; // set only while a script-backed command is running
};

// RAII: makes a ScriptingSession the "current" one for its lifetime. Supports
// nesting (restores whatever was current before on destruction).
class ScriptingSessionScope {
  public:
    explicit ScriptingSessionScope(ScriptingSession session);
    ~ScriptingSessionScope();

    ScriptingSessionScope(const ScriptingSessionScope&)            = delete;
    ScriptingSessionScope& operator=(const ScriptingSessionScope&) = delete;

    // Throws std::runtime_error if no ScriptingSessionScope is currently active.
    [[nodiscard]] static ScriptingSession& Current();

  private:
    ScriptingSession  session_;
    ScriptingSession* previous_;
};

// Narrower RAII: swaps just the active session's context pointer for the
// duration of a single script-backed command invocation. Requires a
// ScriptingSessionScope to already be active.
class CommandContextScope {
  public:
    explicit CommandContextScope(CommandContext& context);
    ~CommandContextScope();

    CommandContextScope(const CommandContextScope&)            = delete;
    CommandContextScope& operator=(const CommandContextScope&) = delete;

  private:
    CommandContext* previous_;
};

} // namespace ned::editor

#endif // NED_EDITOR_SCRIPTINGSESSION_H
