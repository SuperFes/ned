//
// The ned/* Janet-facing API: buffer editing, point movement, and the two
// functions that make Janet code able to extend the editor --
// ned/register-command and ned/define-key.
//

#ifndef NED_JANET_EDITORBINDINGS_H
#define NED_JANET_EDITORBINDINGS_H

#include "Environment.h"

namespace ned::janet {

// Registers ned/insert, ned/forward-char, ned/backward-char, ned/delete-char,
// ned/backward-delete-char, ned/point, ned/buffer-text, ned/register-command,
// ned/define-key, and ned/set-format-command (format-on-save follow-up: the
// shell command save-buffer pipes buffer content through before writing,
// e.g. clang-format -- one process-wide command, not per-mode) into env.
//
// All but register-command/define-key operate on whatever CommandContext is
// currently active (see ScriptingSession) -- calling them outside a running
// command throws, which Janet reports as a normal Janet-level error.
//
// A ned::editor::ScriptingSessionScope must be active whenever any ned/*
// function runs (including a command ned/register-command defined, since
// invoking it re-enters Janet). It does not need to still be the *same*
// scope instance as when Install was called -- Install just wires the
// functions into env; which session is current is resolved per call.
void InstallEditorBindings(Environment& env);

} // namespace ned::janet

#endif // NED_JANET_EDITORBINDINGS_H
