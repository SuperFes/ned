//
// Named color palette for the UI layer -- syntax classes plus editor chrome
// (mode line, echo area, selection, isearch match). Kept in Source/UI/ rather
// than alongside editor::SyntaxClass in Source/Editor/Mode.h: a Theme is
// inherently a TermOx concept (ox::Brush/ox::Color), and Mode.h is kept
// UI-agnostic on purpose (see its header comment).
//
// v1 scope: a small, fixed set of hardcoded C++ themes (DarkTheme/LightTheme)
// selected once at startup, not a Janet-scriptable palette system -- that's a
// deliberate, documented follow-up (see ROADMAP.md's Phase 6 notes), not an
// oversight.
//

#ifndef NED_UI_THEME_H
#define NED_UI_THEME_H

#include <string>

#include <ox/ox.hpp>

#include "Editor/Mode.h"

namespace ned::ui {

struct Theme {
    std::string name;

    // Shared background for ordinary buffer text; foreground varies by
    // syntax class. Keeping one shared background here (rather than one per
    // class) is what makes a genuinely different-looking LightTheme possible
    // without repeating it five times.
    ox::Color background;
    ox::Color defaultForeground;
    ox::Color commentForeground;
    ox::Color stringForeground;
    ox::Color keywordForeground;
    ox::Color numberForeground;
    // Added alongside SyntaxClass's own expansion (bundle-remaining-grammars
    // follow-up) to give real tree-sitter highlights.scm captures
    // JetBrains-IDE-level visual distinction, not just the original 5-color
    // set -- see SyntaxClass's own doc comment in Mode.h for why.
    ox::Color docCommentForeground;
    ox::Color stringEscapeForeground;
    ox::Color controlKeywordForeground;
    ox::Color functionForeground;
    ox::Color functionBuiltinForeground;
    ox::Color typeForeground;
    ox::Color typeBuiltinForeground;
    ox::Color constantForeground;
    ox::Color constantBuiltinForeground;
    ox::Color variableForeground;
    ox::Color variableBuiltinForeground;
    ox::Color parameterForeground;
    ox::Color propertyForeground;
    ox::Color operatorForeground;
    ox::Color punctuationForeground;
    ox::Color tagForeground;
    ox::Color attributeForeground;
    ox::Color namespaceForeground;

    ox::Color     modeLineForeground;
    ox::TrueColor modeLineGradientStart;
    ox::TrueColor modeLineGradientEnd;

    ox::Brush echoArea;

    // Line-number gutter; currentLineNumberForeground is used only for the
    // row point is currently on, so it stands out from the rest.
    ox::Color lineNumberForeground;
    ox::Color currentLineNumberForeground;

    // Overlay backgrounds for in-buffer highlights; the underlying glyph's
    // foreground (from BrushFor above) is kept as-is so syntax coloring
    // stays visible underneath a selection or a search match.
    ox::Color selectionBackground;
    ox::Color isearchMatchBackground;

    // Tab bar (tab-bar follow-up): tabBar is the brush for inactive tabs and
    // the row's own fill; activeTab is the visually distinct brush for
    // whichever tab is the currently active buffer.
    ox::Brush tabBar;
    ox::Brush activeTab;

    // ox::ScrollBar's track/thumb brush (scroll-bar follow-up).
    ox::Brush scrollBar;

    // ScrollArrowButton's brush when scrolling further in that direction
    // isn't currently possible (e.g. already at the top/bottom, or the
    // whole buffer fits on screen) -- scroll-bar follow-up.
    ox::Brush scrollBarDisabled;

    // Foreground for a control-byte hex placeholder (binary-rendering
    // follow-up) -- see BufferView::paint()'s own comment for why a raw
    // control byte is never sent to the terminal at all.
    ox::Color binaryForeground;

    [[nodiscard]] ox::Brush BrushFor(editor::SyntaxClass cls) const;
};

[[nodiscard]] Theme DarkTheme();
[[nodiscard]] Theme LightTheme();

} // namespace ned::ui

#endif // NED_UI_THEME_H
