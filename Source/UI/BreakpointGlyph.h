//
// debug-panel: the breakpoint marker vocabulary, in one place because two
// surfaces now draw it -- BufferView's gutter column (Paint.cpp) and the
// debug panel's breakpoint section (DebugPanel.cpp). Glyph says what KIND of
// breakpoint it is, color says what STATE it is in; the two are independent,
// which is why they are two functions rather than one lookup.
//
// The glyphs are the gutter's own, unchanged: plain-single-width Unicode from
// the geometric-shapes range, the same discipline the diagnostic glyphs and
// scroll arrows follow.
//

#ifndef NED_UI_BREAKPOINTGLYPH_H
#define NED_UI_BREAKPOINTGLYPH_H

#include "Editor/Dap/Manager.h"
#include "Theme.h"

namespace ned::ui {

// ● plain · ◆ conditional · ◇ hit-count · ○ logpoint (which never halts, so
// it reads as the hollow one).
[[nodiscard]] inline const char* BreakpointGlyph(const editor::dap::Manager::Breakpoint& breakpoint) {
    if (!breakpoint.logMessage.empty()) {
        return "○";
    }
    if (!breakpoint.condition.empty()) {
        return "◆";
    }
    if (!breakpoint.hitCondition.empty()) {
        return "◇";
    }
    return "●";
}

// Disabled recedes ahead of everything else: an adapter's "verified" claim
// is about a breakpoint that is actually set, and a disabled one never was
// (see Manager::Breakpoint::enabled).
[[nodiscard]] inline Color BreakpointGlyphColor(const Theme& theme, bool verified, bool enabled) {
    if (!enabled) {
        return theme.indentGuideForeground;
    }
    return verified ? theme.breakpointMarker : theme.unverifiedBreakpointMarker;
}

} // namespace ned::ui

#endif // NED_UI_BREAKPOINTGLYPH_H
