//
// debug-panel: one process-wide on/off switch for the debugger's inline
// values -- the `name = value` annotation BufferView draws after a line that
// mentions a local, while the debuggee is stopped in that file.
// InlineDiagnostics.h's exact pattern (mutex-guarded static, default on,
// set from Janet via ned/set-inline-debug-values and toggled live via
// toggle-inline-debug-values).
//
// Purely a display switch: the values themselves (Dap::Manager::FrameLocals,
// the debug panel's Variables section, the *debug* buffer) are unaffected
// either way. It exists because the annotation is genuinely intrusive on a
// dense line, and because a frame with many locals annotates nearly every
// line of it.
//
// EndOfLine is the only style: an inline value never costs a screen row, for
// the same reason InlineDiagnosticStyle::EndOfLine is the diagnostics
// default -- a value appearing or changing while stepping must not shove
// the code below it up and down.
//

#ifndef NED_EDITOR_INLINEDEBUGVALUES_H
#define NED_EDITOR_INLINEDEBUGVALUES_H

namespace ned::editor {

void               SetInlineDebugValuesEnabled(bool enabled);
[[nodiscard]] bool InlineDebugValuesEnabled();

} // namespace ned::editor

#endif // NED_EDITOR_INLINEDEBUGVALUES_H
