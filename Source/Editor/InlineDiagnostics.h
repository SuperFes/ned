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

// How an inline diagnostic is drawn. This exists because the two styles
// differ in one way that is not cosmetic at all: whether showing a diagnostic
// changes the number of screen rows a line occupies.
//
//  - EndOfLine draws the message after the line's own text, on the line's
//    own (last) row. A line is always exactly as many rows tall as its text
//    needs, so a diagnostic appearing, moving or clearing never shifts
//    anything else on screen.
//  - Callout is the original jank-compiler-style block: a row of its own
//    below the line, carets under the flagged span, then the message. It
//    points at the exact columns, which EndOfLine cannot, and it costs an
//    extra row that comes and goes with the diagnostic.
//
// EndOfLine is the default. Reported twice against a live session: typing
// makes diagnostics appear and clear constantly, and under Callout every one
// of those shoved every line below it up or down a row. The movement was
// traced to exactly this by counting rows, after frames from a screencast
// showed two annotation rows in one frame and one in the next.
enum class InlineDiagnosticStyle {
    EndOfLine,
    Callout,
};

void               SetInlineDiagnosticsEnabled(bool enabled);
[[nodiscard]] bool InlineDiagnosticsEnabled();

void                                SetInlineDiagnosticStyle(InlineDiagnosticStyle style);
[[nodiscard]] InlineDiagnosticStyle GetInlineDiagnosticStyle();

} // namespace ned::editor

#endif // NED_EDITOR_INLINEDIAGNOSTICS_H
