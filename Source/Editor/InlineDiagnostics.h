//
// inline-diagnostics follow-up. One process-wide on/off switch for the
// jank-compiler-style inline diagnostic annotation rows BufferView renders
// under a line carrying an LSP diagnostic (carets under the span plus the
// message text) -- mirrors CodeFoldSettings.h's exact pattern. Default on;
// configured from Janet (ned/set-inline-diagnostics) or toggled live via
// the toggle-inline-diagnostics command. Purely a display switch: the
// diagnostics themselves (gutter icon, underline, lsp-show-diagnostic echo)
// are unaffected either way.
//

#ifndef NED_EDITOR_INLINEDIAGNOSTICS_H
#define NED_EDITOR_INLINEDIAGNOSTICS_H

namespace ned::editor {

void               SetInlineDiagnosticsEnabled(bool enabled);
[[nodiscard]] bool InlineDiagnosticsEnabled();

} // namespace ned::editor

#endif // NED_EDITOR_INLINEDIAGNOSTICS_H
