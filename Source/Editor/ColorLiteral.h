//
// Colour literals in text, recognised without a language server.
//
// This is the native half of `textDocument/documentColor` --
// `Lsp::Manager::DocumentColorSpans` layers a server's own findings over what
// this produces. The split exists because the protocol half, measured against
// every server installed here (`Tools/lsp-capability-probe.py`), reaches
// exactly one of them: only lua-language-server advertises `colorProvider`,
// and no CSS-family server is installed at all. A swatch that appears only in
// Lua would be a feature nobody sees, where a recogniser owned here works in
// CSS, in a theme file, in a config file and in a comment -- the same
// "ned computes it from its own text, a server may add to it" shape
// `foldingRange` and `selectionRange` already settled on.
//
// Everything here is pure: text in, spans out. No buffer, no mode, no
// parse tree. That is what lets the scan run over a viewport window on a
// huge file and be unit-tested on string literals.
//
// Two spellings are opt-in per language rather than universal, because
// outside a stylesheet they are not colours at all: `#abc` (3-digit hex) is
// how a comment starts in half the languages ned parses, and `red` is an
// ordinary identifier in all of them. `ColorLiteralOptions` carries that
// choice; `LanguageDefinition::colorLiterals` is where a language makes it.
//
// Out of scope deliberately, not forgotten: `lab()`/`lch()`/`oklab()`/
// `oklch()`. Recognising them is easy; the problem is the round trip. Those
// spaces are wider than sRGB, so offering "the same colour as #rrggbb" for an
// out-of-gamut `oklch()` would silently rewrite a P3 colour as its clipped
// sRGB twin -- data loss wearing a conversion's clothes. Admitting them needs
// a gamut-mapping policy first, which is its own decision.
//

#ifndef NED_EDITOR_COLORLITERAL_H
#define NED_EDITOR_COLORLITERAL_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ned::editor {

// Components in 0..1, which is the shape `textDocument/documentColor` puts on
// the wire -- so an LSP-sourced colour and a natively-scanned one are the same
// type, and the presentation formatter below serves both.
struct ColorValue {
    double red   = 0.0;
    double green = 0.0;
    double blue  = 0.0;
    double alpha = 1.0;

    [[nodiscard]] bool operator==(const ColorValue&) const = default;
};

// How a colour is spelled. Doubles as the presentation menu's row identity:
// `color-at-point` offers one row per syntax this colour can be written in.
enum class ColorSyntax : std::uint8_t {
    HexShort,      // #f0a
    HexShortAlpha, // #f0a8
    Hex,           // #ff00aa
    HexAlpha,      // #ff00aa80
    Rgb,           // rgb(255, 0, 170)     -- also parses the modern rgb(255 0 170) form
    Rgba,          // rgba(255, 0, 170, 0.5)
    Hsl,           // hsl(320, 100%, 50%)
    Hsla,          // hsla(320, 100%, 50%, 0.5)
    Hwb,           // hwb(320 0% 0%)       -- space-separated only; CSS defines no comma form
    Named,         // tomato
    // A colour somebody else reported (a language server's own
    // documentColor) whose spelling is not ned's to claim. Never produced by
    // the scan, never offered as a presentation, and FormatColor declines it.
    Unknown,
};

struct ColorLiteral {
    std::size_t begin = 0;
    std::size_t end   = 0; // exclusive
    ColorValue  color;
    ColorSyntax syntax = ColorSyntax::Hex;

    [[nodiscard]] bool operator==(const ColorLiteral&) const = default;
};

// The spellings a given buffer's language admits. The defaults are the two
// that are unambiguous in any language: a 6/8-digit hex literal, and a
// functional notation whose own name says what it is.
struct ColorLiteralOptions {
    bool shortHex    = false; // #rgb / #rgba
    bool namedColors = false; // tomato, rebeccapurple, ...
};

// Every colour literal in `text`, in ascending order, non-overlapping.
// Offsets are relative to `text` -- a windowed caller adds its own base.
[[nodiscard]] std::vector<ColorLiteral> ScanColorLiterals(std::string_view           text,
                                                          const ColorLiteralOptions& options);

// The literal in `literals` (ascending and non-overlapping, as
// ScanColorLiterals produces) containing `offset`, or nullptr. A literal that
// merely touches `offset` at its exclusive end counts, so point sitting just
// past the last character of `#ff00aa` still finds it -- the same courtesy
// every word-at-point in this codebase extends.
//
// Takes an already-scanned list rather than text, because its one production
// caller acts on the scan merged with a language server's own findings, and
// the containment rule must not exist in two places.
[[nodiscard]] const ColorLiteral* ColorLiteralContaining(const std::vector<ColorLiteral>& literals,
                                                         std::size_t                      offset);

// The literal text for `color` in `syntax`, or nullopt when this colour cannot
// be written that way at all: `HexShort` needs every channel to be a repeated
// nibble, and `Named` needs an exact match in the CSS table.
//
// Integer-rounded where a human would round (hsl's degrees and percentages),
// but only when the rounded form still reproduces the original 8-bit sRGB;
// otherwise the components carry two decimals, so accepting a presentation
// never silently shifts the colour.
[[nodiscard]] std::optional<std::string> FormatColor(const ColorValue& color, ColorSyntax syntax);

// One entry per way `color` can be spelled, in menu order, skipping the ones
// that cannot represent it. `options` narrows it the same way it narrows the
// scan -- there is no point offering `tomato` in a language where a later
// scan would not recognise it as a colour.
struct ColorPresentation {
    ColorSyntax syntax;
    std::string text;
};

[[nodiscard]] std::vector<ColorPresentation> ColorPresentations(const ColorValue&          color,
                                                                const ColorLiteralOptions& options);

// sRGB <-> HSL, the pair every hue-notation format here is built on and the
// pair an interactive picker needs in both directions. Public rather than
// private to the scan because a picker adjusting H/S/L has to round-trip
// through the same arithmetic the `hsl()` formatter uses, or a value typed in
// one surface would not match the literal the other writes.
//
// RgbToHsl is lossy at the achromatic extremes -- black, white and any grey
// report hue 0 and saturation 0, because no other answer is derivable from
// the colour alone. A caller that wants a hue to survive a trip through grey
// has to remember it itself; this function cannot.
struct HslColor {
    double hue        = 0.0; // degrees, 0..360
    double saturation = 0.0; // 0..1
    double lightness  = 0.0; // 0..1
    double alpha      = 1.0;
};

[[nodiscard]] HslColor   RgbToHsl(const ColorValue& color);
[[nodiscard]] ColorValue HslToRgb(const HslColor& hsl);

// 8-bit sRGB, for painting. Rounds; does not clamp, because every producer
// here already produced in-gamut values.
[[nodiscard]] std::uint8_t ColorChannelToByte(double component);

} // namespace ned::editor

#endif // NED_EDITOR_COLORLITERAL_H
