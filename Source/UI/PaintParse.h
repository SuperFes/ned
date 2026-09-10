//
// Turning a theme author's array (or its one-line string form) into a
// Paint. The grammar is one rule, and this file is the whole of it:
//
//   A paint is an optional axis or pattern keyword, then stop values, with
//   numbers between them as relative weights.
//
// Strings are stops, numbers are weights -- which is also what keeps
// scripting-language nesting out of the parser's way: the caller flattens
// its own array into PaintTokens, so Janet today and jank later share every
// line below. See Docs/Translucency.md's "Authoring gradients without
// becoming a CSS parser".
//
// Deliberately NOT a CSS parser: no nesting except one level for :stack, no
// units, no precedence, no functions. Anything more expressive belongs in
// the scripting language, which is a real one.
//

#ifndef NED_UI_PAINTPARSE_H
#define NED_UI_PAINTPARSE_H

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Paint.h"

namespace ned::ui {

// One element of an authored array. `Nested` exists only for :stack, whose
// layers are themselves paints -- the string form cannot express it, and
// says so by refusing rather than guessing.
struct PaintToken {
    enum class Kind : std::uint8_t { Number,
                                     Text,
                                     Nested };

    Kind                    kind   = Kind::Text;
    double                  number = 0.0;
    std::string             text;
    std::vector<PaintToken> nested;

    [[nodiscard]] static PaintToken Num(double value) {
        return PaintToken{.kind = Kind::Number, .number = value};
    }
    [[nodiscard]] static PaintToken Str(std::string value) {
        return PaintToken{.kind = Kind::Text, .text = std::move(value)};
    }
    [[nodiscard]] static PaintToken Sub(std::vector<PaintToken> value) {
        return PaintToken{.kind = Kind::Nested, .nested = std::move(value)};
    }
};

// How `$name` resolves. Both hooks are optional: an unset one simply means
// that kind of reference cannot be used, and the parse fails with a message
// saying so rather than silently painting the wrong thing.
struct PaintContext {
    // A theme slot: $bg, $fg, $accent, ... (ThemeSlots below).
    std::function<std::optional<Color>(std::string_view)> slot;
    // A previously registered named paint: $brand, $glass, ...
    std::function<std::optional<Paint>(std::string_view)> named;
};

// Parses one colour token: "#rrggbb", "#rrggbbaa", "default", or a slot
// reference with up to three postfix adjustments -- "$bg+8" (lighten 8%),
// "$accent1-12" (darken), "$bg/60" (60% alpha). Adjustments apply left to
// right, so "$bg+8/60" lifts and then fades.
[[nodiscard]] std::optional<Color> ParseColorStop(std::string_view token, const PaintContext& context);

// The parse proper. Returns nullopt and fills `error` (when non-null) on
// anything malformed -- a theme that says something impossible should be
// told so, not quietly approximated.
[[nodiscard]] std::optional<Paint> ParsePaint(const std::vector<PaintToken>& tokens, const PaintContext& context,
                                              std::string* error = nullptr);

// The one-line string form, for the flat key=value theme file and for
// people who would rather type one token: "y #1e1e2eff 3 #313244cc".
// Whitespace-separated; numeric tokens are weights; no nesting.
[[nodiscard]] std::optional<Paint> ParsePaint(std::string_view line, const PaintContext& context,
                                              std::string* error = nullptr);

// Back to the one-line form, the only shape C++ parses. A Stack
// cannot be written this way (no nesting) and comes back empty.
[[nodiscard]] std::string PaintToString(const Paint& paint);

// Translucency phase 7c: a shadow spec -- `dx dy radius colour`, e.g.
// "2 1 2 #00000060". Positional rather than keyword-ish because all four
// always matter and there is no sensible default for any of them once a
// theme has decided to author one at all.
//
// The colour goes through ParseColorStop, so it takes the same `$slot`
// references and `#rrggbbaa` literals every paint stop does -- a shadow that
// tints toward the theme's own background rather than flat black is a
// legitimate thing to want. std::nullopt for anything that does not parse.
[[nodiscard]] std::optional<Shadow> ParseShadow(std::string_view line, const PaintContext& context);

// The slot names a theme exposes to $-references, resolved against the
// active Theme's own fields -- deliberately a fixed, small table rather
// than every one of the ~70 fields, since a gradient wants the handful of
// colours that define a theme's character.
[[nodiscard]] std::vector<std::string> ThemeSlots();

} // namespace ned::ui

#endif // NED_UI_PAINTPARSE_H
