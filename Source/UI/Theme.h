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

#include <ftxui/screen/cell.hpp>
#include <ftxui/screen/color.hpp>

#include "Editor/Mode.h"

namespace ned::ui {

// A small, introspectable color representation (TermOx -> FTXUI migration)
// -- ftxui::Color itself is intentionally opaque (no public accessor for
// which of Default/Palette16/TrueColor it holds, nor its stored RGB bytes
// back out), which is fine for painting but genuinely insufficient for
// ThemeFile.cpp's own round-trip text serialization, which needs to know
// exactly that. Mirrors escape::Color's own three-way shape (the TermOx-era
// XColor/TrueColor/TermColor variant this replaces), just re-targeted at
// FTXUI's three color kinds. ToFtxui() is the only place this ever needs to
// become a real ftxui::Color, at actual paint time.
struct Color {
    enum class Kind : std::uint8_t { Default,
                                     Palette16,
                                     TrueColor };

    Kind         kind         = Kind::Default;
    std::uint8_t paletteIndex = 0;             // valid when kind == Palette16
    std::uint8_t red = 0, green = 0, blue = 0; // valid when kind == TrueColor

    [[nodiscard]] constexpr bool operator==(const Color&) const = default;

    [[nodiscard]] ftxui::Color ToFtxui() const;

    // Matches ox::RGB's own hex-literal convenience (e.g. RGB(0x2b2b40)) --
    // every true-color use in this codebase already writes colors this way.
    [[nodiscard]] static constexpr Color RGB(std::uint32_t hex) {
        return Color{.kind  = Kind::TrueColor,
                     .red   = static_cast<std::uint8_t>((hex >> 16) & 0xFF),
                     .green = static_cast<std::uint8_t>((hex >> 8) & 0xFF),
                     .blue  = static_cast<std::uint8_t>(hex & 0xFF)};
    }
    [[nodiscard]] static constexpr Color RGB(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
        return Color{.kind = Kind::TrueColor, .red = r, .green = g, .blue = b};
    }
    [[nodiscard]] static constexpr Color Palette(std::uint8_t index) {
        return Color{.kind = Kind::Palette16, .paletteIndex = index};
    }

    // Named 16-color constants -- kept under the same Black/Red/.../Bright*
    // names the old ox::XColor constants used (rather than FTXUI's own
    // GrayLight/RedLight/... naming) so every existing color choice in
    // Theme.cpp reads unchanged. Index values match ftxui::Color::Palette16's
    // own numbering exactly (0=Black ... 15=White), which is itself the same
    // numbering the old ox::XColor constants used -- confirmed, not assumed,
    // by reading FTXUI's real color.hpp.
    //
    // Declared here, defined just below (outside the class body) as C++17
    // inline variables -- a class can't in-class-initialize a static member
    // of its own type before the class itself is complete (a real compile
    // error hit while porting, not a style choice), so definition has to
    // wait until Color is a complete type.
    static const Color Default;
    static const Color Black, Red, Green, Yellow, Blue, Magenta, Cyan, White;
    static const Color BrightBlack, BrightRed, BrightGreen, BrightYellow, BrightBlue, BrightMagenta, BrightCyan,
        BrightWhite;
};

inline constexpr Color Color::Default{};
inline constexpr Color Color::Black         = Color::Palette(0);
inline constexpr Color Color::Red           = Color::Palette(1);
inline constexpr Color Color::Green         = Color::Palette(2);
inline constexpr Color Color::Yellow        = Color::Palette(3);
inline constexpr Color Color::Blue          = Color::Palette(4);
inline constexpr Color Color::Magenta       = Color::Palette(5);
inline constexpr Color Color::Cyan          = Color::Palette(6);
inline constexpr Color Color::White         = Color::Palette(7);
inline constexpr Color Color::BrightBlack   = Color::Palette(8);
inline constexpr Color Color::BrightRed     = Color::Palette(9);
inline constexpr Color Color::BrightGreen   = Color::Palette(10);
inline constexpr Color Color::BrightYellow  = Color::Palette(11);
inline constexpr Color Color::BrightBlue    = Color::Palette(12);
inline constexpr Color Color::BrightMagenta = Color::Palette(13);
inline constexpr Color Color::BrightCyan    = Color::Palette(14);
inline constexpr Color Color::BrightWhite   = Color::Palette(15);

// A pared-down replacement for esc::Brush -- background/foreground plus
// individual bool trait fields rather than a combinable Trait bitmask, since
// this codebase only ever used Bold and Italic (checked directly, not
// assumed) and ftxui::Cell already stores traits as individual bools, making
// "apply a Brush to a Cell" a direct field-by-field copy with no bitmask
// testing needed.
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
    // foreground/bold/italic/underlined/strikethrough actually become an
    // ftxui::Cell's own fields, used by every widget's Paint() the same way
    // old widgets wrote ox::Glyph{.symbol = ..., .brush = someBrush} directly.
    void ApplyTo(ftxui::Cell& cell) const {
        cell.background_color = background.ToFtxui();
        cell.foreground_color = foreground.ToFtxui();
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
    // A gradient endpoint can't meaningfully be "default" or a palette
    // index -- kept as plain Color (not restricted further) since
    // ftxui::Color::Interpolate accepts any Color and produces a sensible
    // result either way; ThemeFile.cpp's own serialization is what actually
    // enforces "hex only" for these two keys.
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

    // Markdown-highlighting follow-up: MarkupMarker's own dim/muted Color --
    // deliberately not reusing any existing chrome field, since this needs
    // to read as visually receded relative to everything else, unlike any
    // existing foreground color. Link reuses the existing linkForeground
    // field above (Mode.h's SyntaxClass::Link doc comment explains why).
    Color markupMarkerForeground;

    [[nodiscard]] Brush BrushFor(editor::SyntaxClass cls) const;

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
