//
// Named color palette for the UI layer -- syntax classes plus editor chrome
// (mode line, echo area, selection, isearch match). Kept in Source/UI/ rather
// than alongside editor::SyntaxClass in Source/Editor/Mode.h: a Theme is
// inherently a TUI-library concept, and Mode.h is kept UI-agnostic on
// purpose (see its header comment).
//
// v1 scope: a small, fixed set of hardcoded C++ themes (DarkTheme/LightTheme)
// selected once at startup -- still true for *theme selection* itself
// (Phase 6 notes, ROADMAP.md). Per-SyntaxClass style overrides ARE now
// Janet-scriptable, though (Janet-configurable-syntax-theme follow-up):
// BrushFor() merges editor::SyntaxOverrideFor(cls) (Editor/SyntaxTheme.h) on
// top of whichever built-in Brush this switch computes below -- see that
// merge's own comment at BrushFor()'s definition.
//

#ifndef NED_UI_THEME_H
#define NED_UI_THEME_H

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "Editor/Mode.h"
#include "UI/Widget.h"

namespace ned::ui {

// Color has moved to Widget.h (FTXUI -> Notcurses migration): Cell (also
// Widget.h) needs to store one directly, so Widget.h can no longer depend
// on Theme.h the way it would if Color stayed here. Still the same
// Default/Palette16/TrueColor shape, same rationale (ThemeFile.cpp's own
// round-trip text serialization needs the kind/RGB bytes back out, unlike
// an opaque library color type) -- see Widget.h's own comment on Color.

// A pared-down replacement for esc::Brush -- background/foreground plus
// individual bool trait fields rather than a combinable Trait bitmask, since
// this codebase only ever used Bold and Italic (checked directly, not
// assumed) and Cell (Widget.h) already stores traits as individual bools,
// making "apply a Brush to a Cell" a direct field-by-field copy with no
// bitmask testing needed.
struct Brush {
    Color background = Color::Default;
    Color foreground = Color::Default;
    bool  bold       = false;
    bool  italic     = false;
    // Org-mode syntax-highlighting follow-up: back Org's own _underline_/
    // +strikethrough+ inline markup -- confirmed real ftxui::Cell fields
    // (not assumed) by reading cell.hpp directly before relying on them.
    bool underlined    = false;
    bool strikethrough = false;

    [[nodiscard]] constexpr bool operator==(const Brush&) const = default;

    // Paints this Brush onto a real Cell -- the one place background/
    // foreground/bold/italic/underlined/strikethrough actually become a
    // Cell's own fields, used by every widget's Paint() the same way old
    // widgets wrote ox::Glyph{.symbol = ..., .brush = someBrush} directly.
    // A plain field-by-field copy now (FTXUI -> Notcurses migration) --
    // Cell's foreground_color/background_color are this file's own Color
    // type directly, so there's no per-cell ToFtxui()-style conversion left
    // to do here at all; Screen::Flush (Widget.cpp) is the only place a
    // Color still becomes a real terminal color.
    void ApplyTo(Cell& cell) const {
        cell.background_color = background;
        cell.foreground_color = foreground;
        cell.bold             = bold;
        cell.italic           = italic;
        cell.underlined       = underlined;
        cell.strikethrough    = strikethrough;
    }
};

struct Theme {
    std::string name;

    // Shared background for ordinary buffer text; foreground varies by
    // syntax class. Keeping one shared background here (rather than one per
    // class) is what makes a genuinely different-looking LightTheme possible
    // without repeating it five times.
    Color background;
    Color defaultForeground;
    Color commentForeground;
    Color stringForeground;
    Color keywordForeground;
    Color numberForeground;
    // Added alongside SyntaxClass's own expansion (bundle-remaining-grammars
    // follow-up) to give real tree-sitter highlights.scm captures
    // JetBrains-IDE-level visual distinction, not just the original 5-color
    // set -- see SyntaxClass's own doc comment in Mode.h for why.
    Color docCommentForeground;
    Color stringEscapeForeground;
    Color controlKeywordForeground;
    Color functionForeground;
    Color functionBuiltinForeground;
    Color typeForeground;
    Color typeBuiltinForeground;
    Color constantForeground;
    Color constantBuiltinForeground;
    Color variableForeground;
    Color variableBuiltinForeground;
    Color parameterForeground;
    Color propertyForeground;
    Color operatorForeground;
    Color punctuationForeground;
    Color tagForeground;
    Color attributeForeground;
    Color namespaceForeground;
    // generic-tree-sitter-highlighting follow-up -- see SyntaxClass's own
    // doc comment in Mode.h for what each newly-split-out class covers.
    Color keywordModifierForeground;
    Color methodForeground;
    Color constructorForeground;
    Color labelForeground;
    Color returnTypeForeground;
    Color includePathForeground;

    Color modeLineForeground;
    // Any Color works as a gradient endpoint: Interpolate approximates
    // Default/Palette16 endpoints via a fixed RGB table, and returns equal
    // endpoints unchanged -- which is exactly how the ANSI fallback themes
    // express "no gradient, stay a real palette color" (theme-editing
    // follow-up: ThemeFile's old hex-only restriction on these keys was
    // dropped for the same reason).
    Color modeLineGradientStart;
    Color modeLineGradientEnd;

    Brush echoArea;

    // Line-number gutter; currentLineNumberForeground is used only for the
    // row point is currently on, so it stands out from the rest.
    Color lineNumberForeground;
    Color currentLineNumberForeground;

    // Overlay backgrounds for in-buffer highlights; the underlying glyph's
    // foreground (from BrushFor above) is kept as-is so syntax coloring
    // stays visible underneath a selection or a search match.
    Color selectionBackground;
    Color isearchMatchBackground;
    // snippet-expansion follow-up: the live snippet session's *active*
    // tabstop field (mirrors deliberately unhighlighted in v1) -- same
    // keep-the-glyph-foreground overlay contract as the two above.
    Color snippetFieldBackground;

    // Tab bar (tab-bar follow-up): tabBar is the brush for inactive tabs and
    // the row's own fill; activeTab is the visually distinct brush for
    // whichever tab is the currently active buffer.
    Brush tabBar;
    Brush activeTab;

    // The scroll bar's track/thumb brush (scroll-bar follow-up).
    Brush scrollBar;

    // ScrollArrowButton's brush when scrolling further in that direction
    // isn't currently possible (e.g. already at the top/bottom, or the
    // whole buffer fits on screen) -- scroll-bar follow-up.
    Brush scrollBarDisabled;

    // Foreground for a control-byte hex placeholder (binary-rendering
    // follow-up) -- see BufferView::paint()'s own comment for why a raw
    // control byte is never sent to the terminal at all.
    Color binaryForeground;

    // Foreground for LSP ghost-text completion suggestions (hover/
    // completion follow-up) -- rendered italic (see BufferView::Paint()'s
    // ghost-text comment) atop this dim color, the same "UI chrome, not
    // syntax" reasoning binaryForeground/linkForeground already establish.
    Color ghostTextForeground;

    // Foreground for a collapsed Org link's own displayText (links
    // follow-up) -- see BufferView::Paint()'s own "descriptive links"
    // comment. Not a SyntaxClass (links aren't a tree-sitter capture),
    // hence a dedicated field here rather than a BrushFor() case, the same
    // "UI chrome, not syntax" reasoning binaryForeground/lineNumberForeground
    // already establish.
    Color linkForeground;

    // line-truncation-indicator follow-up: foreground for the "»" glyph
    // overwriting a clipped line's own last column when it's too long for
    // the viewport and wrap is off (see BufferView::Paint()'s own comment
    // -- unreachable under wrap, a wrapped segment never exceeds the
    // viewport width by construction). A muted blue-purple ("blurple"),
    // deliberately distinct from every syntax/UI-chrome color already in
    // use here so it can't be mistaken for anything else, but mid-brightness
    // rather than alarming -- this is a hint, not a warning.
    Color truncationIndicatorForeground;

    // Status-gutter unsaved-change-indicator follow-up: a solid-block
    // marker (background color, not a glyph -- see BufferView::Paint()'s
    // own comment) in the new 1-column status gutter for a line with edits
    // since the buffer was last loaded/saved. The conventional "modified"
    // accent color in every mainstream editor's gutter, deliberately not
    // part of a green-add/red-delete pair -- this is a single-state
    // indicator, not a real diff (see Buffer::UnsavedChangeRanges()'s own
    // doc comment).
    Color unsavedChangeIndicator;

    // LSP client follow-up: solid-block severity markers (same "background
    // color, not a glyph" shape as unsavedChangeIndicator above) for the
    // diagnostics gutter column -- one color per Buffer::Diagnostic::Severity
    // value, in the same order that enum declares them.
    Color diagnosticError;
    Color diagnosticWarning;
    Color diagnosticInformation;
    Color diagnosticHint;

    // DAP client slice 2: the debug gutter column's two marker foregrounds
    // (breakpoint dot, current-execution arrow) and the background wash for
    // the whole line the debuggee is stopped on -- the red-dot/yellow-arrow/
    // highlighted-line convention every mainstream debugger UI shares.
    Color breakpointMarker;
    Color executionMarker;
    Color executionLineBackground;
    // DAP round 2: a dimmer variant of breakpointMarker for a breakpoint
    // the adapter's own setBreakpoints response marked unverified (bad
    // condition syntax, an unreachable line, ...) -- shape (glyph) still
    // communicates plain/conditional/logpoint, this communicates verified
    // state, orthogonally.
    Color unverifiedBreakpointMarker;

    // Multibuffers follow-up: whole-line content backgrounds for the *vcs
    // diff* multibuffer's own added/removed lines -- deliberately a real
    // Theme field (unlike the live diff gutter's marker glyph, which stays
    // a bare Color constant; see BufferView.cpp's own comment on why a
    // content-area wash was removed from the live gutter after user
    // feedback about fighting syntax-highlighted text contrast). Safe here
    // specifically because this multibuffer's excerpt lines carry no syntax
    // highlighting of their own to fight -- there's nothing competing for
    // the same pixels.
    Color diffAddedBackground;
    Color diffRemovedBackground;

    // Whitespace-visualization follow-up: a subtle background wash for
    // trailing whitespace (BufferView::Paint()'s own "is this cell still
    // inside the line's trailing run" check), and the foreground for the
    // vertical indent-guide glyph drawn at each indent-width column within
    // a line's own leading whitespace. Both gated off by default -- see
    // Editor/WhitespaceSettings.h.
    Color trailingWhitespaceBackground;
    Color indentGuideForeground;

    // Org-mode syntax-highlighting follow-up: one Color per new
    // Org-specific SyntaxClass member (Mode.h) -- headline levels cycle
    // through 3 distinct, bold hues; TodoKeyword/DoneKeyword use the
    // universal not-done/done warm/cool convention; Checkbox is one
    // neutral, distinct color; Strong/Emphasis reuse defaultForeground
    // (bold/italic already say "this is emphasized," no new hue needed,
    // matching how real bold/italic text usually reads); Underline/
    // Strikethrough need their own Color since Brush's new underlined/
    // strikethrough bools carry no hue of their own.
    Color headlineLevel1Foreground;
    Color headlineLevel2Foreground;
    Color headlineLevel3Foreground;
    Color todoKeywordForeground;
    Color doneKeywordForeground;
    Color checkboxForeground;
    Color underlineForeground;
    Color strikethroughForeground;

    // Chrome-redesign follow-up: the app-wide border language. `border` is
    // the quiet structural line brush (sidebar frame, tab underline, split
    // dividers); `borderAccent` is the attention pole of the same family
    // (sidebar title, divider during a resize drag, active-tab underline
    // corners, collapsed-sidebar hint glyph). Two poles of one palette, not
    // two unrelated colors -- see DarkTheme()'s own values.
    Brush border;
    Brush borderAccent;

    // Chrome-redesign follow-up: the focused pane's mode-line gradient
    // endpoints (unfocused panes keep modeLineGradientStart/End above) --
    // stored as explicit fields rather than derived at paint time so theme
    // files and --detect-theme control the tint, same reasoning as the
    // base gradient's own fields. Same "hex only" ThemeFile restriction.
    Color modeLineFocusedGradientStart;
    Color modeLineFocusedGradientEnd;

    // Markdown-highlighting follow-up: MarkupMarker's own dim/muted Color --
    // deliberately not reusing any existing chrome field, since this needs
    // to read as visually receded relative to everything else, unlike any
    // existing foreground color. Link reuses the existing linkForeground
    // field above (Mode.h's SyntaxClass::Link doc comment explains why).
    Color markupMarkerForeground;

    [[nodiscard]] Brush BrushFor(editor::SyntaxClass cls) const;

    // exhaustive-highlighting follow-up: the capture-aware overload --
    // BrushFor(cls)'s result with editor::ResolvedCaptureOverride's
    // dotted-chain merge (Editor/SyntaxTheme.h) applied on top, so a
    // per-capture-name override beats a per-SyntaxClass one, which beats
    // the built-in theme -- most specific wins throughout. kNoCapture is
    // exactly BrushFor(cls). BufferView is the only consumer today, through
    // its own generation-checked brush cache (see ResolvedBrush there) --
    // this does a name lookup plus up to a handful of locked map lookups
    // per call, fine per-span, not something to call per rendered codepoint
    // uncached.
    [[nodiscard]] Brush BrushFor(editor::SyntaxClass cls, editor::CaptureId captureId) const;

    // generic-popup follow-up (Phase 3) theme-preview-bug follow-up:
    // plain field-by-field value equality -- lets a cache that holds a
    // Theme reference (rather than a value) detect an in-place theme
    // change the way it already detects everything else it keys on
    // (buffer identity, content generation, ...). Minimap's own raster
    // cache is the first real consumer; see its own doc comment.
    [[nodiscard]] bool operator==(const Theme&) const = default;

  private:
    // The switch of built-in Dark/Light values BrushFor() used to
    // (and still does) compute directly -- split out so BrushFor() can
    // merge editor::SyntaxOverrideFor(cls) on top of it. Private member
    // functions don't affect Theme's own aggregate-initializer status
    // (DarkTheme()/LightTheme()'s Theme{.name = ..., ...} syntax), only
    // data members do.
    [[nodiscard]] Brush BuiltinBrushFor(editor::SyntaxClass cls) const;
};

[[nodiscard]] Theme DarkTheme();
[[nodiscard]] Theme LightTheme();

// ansi-fallback-theme follow-up: Palette16/Default-only counterparts of
// DarkTheme/LightTheme for terminals with neither truecolor nor a 256-color
// palette (e.g. the Linux framebuffer console, TERM=linux: 8 colors), where
// every TrueColor field above would otherwise get quantized down to those 8
// and wash out -- or land black-on-black outright. Deliberately restricted
// to palette indices 0-7 plus Color::Default (never the Bright 8-15 range):
// an 8-color terminal's terminfo may or may not map 8-15 to bold+base, so
// brightness is expressed through Brush bold where a Brush exists and
// forfeited where one doesn't, rather than gambling on indices the terminal
// never advertised. Gradient endpoints are equal on purpose --
// Color::Interpolate returns equal endpoints unchanged (see its own
// comment), so the mode line stays a real palette color instead of an
// interpolated TrueColor approximation.
[[nodiscard]] Theme AnsiDarkTheme();
[[nodiscard]] Theme AnsiLightTheme();

// Picks the ANSI variant matching `theme`'s own polarity: a TrueColor
// background with light-side luminance selects AnsiLightTheme (LightTheme
// itself, or a --detect-theme file probed from a light terminal);
// everything else -- Default (DarkTheme's pass-through background), a dark
// TrueColor, or a palette index, which carries no reliable luminance --
// selects AnsiDarkTheme. Pure; main.cpp calls it once when EventLoop
// reports a limited terminal (see EventLoop::CanTrueColor/PaletteSize).
[[nodiscard]] Theme AnsiFallbackFor(const Theme& theme);

// "#rrggbb" / "x:<0-255>" / "default" hex-token round-trip for a Color --
// moved here from ThemeFile.cpp (Janet-configurable-syntax-theme follow-up)
// since BrushFor()'s override merge needs the parse side too, not just
// ThemeFile's own save/load; ThemeFile.cpp now calls these instead of
// keeping a private duplicate. See ThemeFile.cpp's own header comment for
// the wire format's full rationale (this is the same one, unchanged).
[[nodiscard]] std::string          ColorToToken(const Color& color);
[[nodiscard]] std::optional<Color> ParseColorToken(std::string_view token);

} // namespace ned::ui

#endif // NED_UI_THEME_H
