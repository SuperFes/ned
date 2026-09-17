#include "Theme.h"

#include <array>
#include <cstdlib>

#include "Editor/SyntaxTheme.h"

namespace ned::ui {

Brush Theme::BuiltinBrushFor(editor::SyntaxClass cls) const {
    switch (cls) {
        case editor::SyntaxClass::Comment:
            return Brush{.background = background, .foreground = commentForeground, .italic = true};
        case editor::SyntaxClass::DocComment:
            return Brush{.background = background, .foreground = docCommentForeground, .italic = true};
        case editor::SyntaxClass::String:
            return Brush{.background = background, .foreground = stringForeground};
        case editor::SyntaxClass::StringEscape:
            return Brush{.background = background, .foreground = stringEscapeForeground};
        case editor::SyntaxClass::Keyword:
            return Brush{.background = background, .foreground = keywordForeground, .bold = true};
        case editor::SyntaxClass::ControlKeyword:
            return Brush{.background = background, .foreground = controlKeywordForeground, .bold = true, .italic = true};
        case editor::SyntaxClass::Number:
            return Brush{.background = background, .foreground = numberForeground};
        case editor::SyntaxClass::Function:
            return Brush{.background = background, .foreground = functionForeground};
        case editor::SyntaxClass::FunctionBuiltin:
            return Brush{.background = background, .foreground = functionBuiltinForeground, .bold = true};
        case editor::SyntaxClass::Type:
            return Brush{.background = background, .foreground = typeForeground};
        case editor::SyntaxClass::TypeBuiltin:
            return Brush{.background = background, .foreground = typeBuiltinForeground, .bold = true};
        case editor::SyntaxClass::Constant:
            return Brush{.background = background, .foreground = constantForeground};
        case editor::SyntaxClass::ConstantBuiltin:
            return Brush{.background = background, .foreground = constantBuiltinForeground, .bold = true};
        case editor::SyntaxClass::Variable:
            return Brush{.background = background, .foreground = variableForeground};
        case editor::SyntaxClass::VariableBuiltin:
            return Brush{.background = background, .foreground = variableBuiltinForeground, .italic = true};
        case editor::SyntaxClass::Parameter:
            return Brush{.background = background, .foreground = parameterForeground, .italic = true};
        case editor::SyntaxClass::Property:
            return Brush{.background = background, .foreground = propertyForeground};
        case editor::SyntaxClass::Operator:
            return Brush{.background = background, .foreground = operatorForeground};
        case editor::SyntaxClass::Punctuation:
            return Brush{.background = background, .foreground = punctuationForeground};
        case editor::SyntaxClass::Tag:
            return Brush{.background = background, .foreground = tagForeground, .bold = true};
        case editor::SyntaxClass::Attribute:
            return Brush{.background = background, .foreground = attributeForeground};
        case editor::SyntaxClass::Namespace:
            return Brush{.background = background, .foreground = namespaceForeground, .italic = true};
        case editor::SyntaxClass::KeywordModifier:
            return Brush{.background = background, .foreground = keywordModifierForeground, .bold = true};
        case editor::SyntaxClass::Method:
            return Brush{.background = background, .foreground = methodForeground, .bold = true};
        case editor::SyntaxClass::Constructor:
            return Brush{.background = background, .foreground = constructorForeground, .bold = true};
        case editor::SyntaxClass::Label:
            return Brush{.background = background, .foreground = labelForeground, .italic = true};
        case editor::SyntaxClass::ReturnType:
            return Brush{.background = background, .foreground = returnTypeForeground, .italic = true};
        case editor::SyntaxClass::IncludePath:
            return Brush{.background = background, .foreground = includePathForeground};
        case editor::SyntaxClass::HeadlineLevel1:
            return Brush{.background = background, .foreground = headlineLevel1Foreground, .bold = true};
        case editor::SyntaxClass::HeadlineLevel2:
            return Brush{.background = background, .foreground = headlineLevel2Foreground, .bold = true};
        case editor::SyntaxClass::HeadlineLevel3:
            return Brush{.background = background, .foreground = headlineLevel3Foreground, .bold = true};
        case editor::SyntaxClass::TodoKeyword:
            return Brush{.background = background, .foreground = todoKeywordForeground, .bold = true};
        case editor::SyntaxClass::DoneKeyword:
            return Brush{.background = background, .foreground = doneKeywordForeground, .bold = true};
        case editor::SyntaxClass::Checkbox:
            return Brush{.background = background, .foreground = checkboxForeground};
        case editor::SyntaxClass::Strong:
            return Brush{.background = background, .foreground = defaultForeground, .bold = true};
        case editor::SyntaxClass::Emphasis:
            return Brush{.background = background, .foreground = defaultForeground, .italic = true};
        case editor::SyntaxClass::Underline:
            return Brush{.background = background, .foreground = underlineForeground, .underlined = true};
        case editor::SyntaxClass::Strikethrough:
            return Brush{.background = background, .foreground = strikethroughForeground, .strikethrough = true};
        case editor::SyntaxClass::MarkupMarker:
            return Brush{.background = background, .foreground = markupMarkerForeground};
        case editor::SyntaxClass::DiffAdded:
            return Brush{.background = background, .foreground = diffAddedForeground};
        case editor::SyntaxClass::DiffRemoved:
            return Brush{.background = background, .foreground = diffRemovedForeground};
        case editor::SyntaxClass::DiffChanged:
            return Brush{.background = background, .foreground = diffChangedForeground};
        case editor::SyntaxClass::Link:
            return Brush{.background = background, .foreground = linkForeground, .underlined = true};
        case editor::SyntaxClass::Default:
        default:
            return Brush{.background = background, .foreground = defaultForeground};
    }
}

// Janet-configurable-syntax-theme follow-up: merges editor::SyntaxOverrideFor(cls)
// on top of BuiltinBrushFor's own Dark/Light value -- an unset override
// field keeps the built-in one, a set field replaces it. Not gated behind
// a generation-checked cache: a mutex lock plus a small std::map lookup is
// genuine nanosecond-class overhead, not the heap-allocation class of cost
// that actually regressed the fold-gutter code earlier this session
// (foldHeaderLineToBlock_'s own history, BufferView.h) -- implemented
// straightforwardly first, matching this codebase's "prove it before
// optimizing" discipline; revisit only if a real [Performance] test says
// otherwise.
namespace {

    // The one "apply a partial override onto a concrete Brush" merge, shared
    // by both BrushFor overloads below -- an unset field keeps what's
    // already there, a set field replaces it.
    void ApplyOverride(Brush& brush, const editor::SyntaxStyleOverride& override) {
        if (override.foreground) {
            if (const auto c = ParseColorToken(*override.foreground)) {
                brush.foreground = *c;
            }
        }
        if (override.background) {
            if (const auto c = ParseColorToken(*override.background)) {
                brush.background = *c;
            }
        }
        if (override.bold) {
            brush.bold = *override.bold;
        }
        if (override.italic) {
            brush.italic = *override.italic;
        }
        if (override.underlined) {
            brush.underlined = *override.underlined;
        }
        if (override.strikethrough) {
            brush.strikethrough = *override.strikethrough;
        }
    }

} // namespace

Brush Theme::BrushFor(editor::SyntaxClass cls) const {
    Brush brush = BuiltinBrushFor(cls);
    ApplyOverride(brush, editor::SyntaxOverrideFor(cls));
    return brush;
}

// exhaustive-highlighting follow-up -- see the header. Applied after the
// class-level merge so the capture chain (itself most-specific-first, see
// ResolvedCaptureOverride) wins field-by-field over the class override.
Brush Theme::BrushFor(editor::SyntaxClass cls, editor::CaptureId captureId) const {
    Brush brush = BrushFor(cls);
    if (captureId != editor::kNoCapture) {
        const std::string name = editor::CaptureNameForId(captureId);
        if (!name.empty()) {
            ApplyOverride(brush, editor::ResolvedCaptureOverride(name));
        }
    }
    return brush;
}

namespace {

    char HexDigit(int nibble) {
        return static_cast<char>(nibble < 10 ? '0' + nibble : 'a' + (nibble - 10));
    }

    std::string TrueColorToHex(const Color& c) {
        // Alpha is written only when there is any -- an opaque colour keeps
        // the exact six-digit form every theme file already contains, so
        // the format extension costs no churn in existing files.
        const bool                        translucent = !c.Opaque();
        const std::array<std::uint8_t, 4> channels{c.red, c.green, c.blue, c.alpha};
        std::string                       out = translucent ? "#00000000" : "#000000";
        for (std::size_t i = 0; i < (translucent ? 4U : 3U); ++i) {
            out[1 + i * 2]     = HexDigit(channels[i] >> 4);
            out[1 + i * 2 + 1] = HexDigit(channels[i] & 0x0F);
        }
        return out;
    }

    std::optional<int> ParseHexNibble(char c) {
        if (c >= '0' && c <= '9') {
            return c - '0';
        }
        if (c >= 'a' && c <= 'f') {
            return 10 + (c - 'a');
        }
        if (c >= 'A' && c <= 'F') {
            return 10 + (c - 'A');
        }
        return std::nullopt;
    }

    std::optional<std::uint8_t> ParseHexByte(std::string_view text) {
        if (text.size() != 2) {
            return std::nullopt;
        }
        const auto high = ParseHexNibble(text[0]);
        const auto low  = ParseHexNibble(text[1]);
        if (!high || !low) {
            return std::nullopt;
        }
        return static_cast<std::uint8_t>((*high << 4) | *low);
    }

    std::optional<Color> ParseHexColor(std::string_view token) {
        if ((token.size() != 7 && token.size() != 9) || token[0] != '#') {
            return std::nullopt;
        }
        const auto r = ParseHexByte(token.substr(1, 2));
        const auto g = ParseHexByte(token.substr(3, 2));
        const auto b = ParseHexByte(token.substr(5, 2));
        if (!r || !g || !b) {
            return std::nullopt;
        }
        if (token.size() == 7) {
            return Color::RGB(*r, *g, *b);
        }
        const auto a = ParseHexByte(token.substr(7, 2));
        if (!a) {
            return std::nullopt;
        }
        return Color::RGB(*r, *g, *b).WithAlpha(static_cast<std::uint8_t>(*a));
    }

} // namespace

// Moved here from ThemeFile.cpp (Janet-configurable-syntax-theme follow-up)
// -- see Theme.h's own doc comment on these two for why.
namespace {

    // xterm's own 256-colour layout: 0-15 are the named palette (whose RGB
    // ColorToRgb8 already knows), 16-231 a 6x6x6 cube, 232-255 a 24-step
    // grey ramp. Only ever used to read a legacy "x:<n>" token.
    Color LegacyPaletteToRgb(int index) {
        if (index < 16) {
            std::uint8_t r = 0, g = 0, b = 0;
            ColorToRgb8(Color::Palette(static_cast<std::uint8_t>(index)), r, g, b);
            return Color::RGB(r, g, b);
        }
        if (index < 232) {
            constexpr int kLevels[6] = {0, 95, 135, 175, 215, 255};
            const int     offset     = index - 16;
            return Color::RGB(static_cast<std::uint8_t>(kLevels[(offset / 36) % 6]),
                              static_cast<std::uint8_t>(kLevels[(offset / 6) % 6]),
                              static_cast<std::uint8_t>(kLevels[offset % 6]));
        }
        const auto grey = static_cast<std::uint8_t>(8 + 10 * (index - 232));
        return Color::RGB(grey, grey, grey);
    }

} // namespace

std::string ColorToToken(const Color& color) {
    switch (color.kind) {
        case Color::Kind::TrueColor:
            return TrueColorToHex(color);
        case Color::Kind::Palette16: {
            // Unreachable from a theme -- themes are truecolor throughout --
            // but Color itself still carries palette indices for the
            // embedded terminal, so serialize the RGB we would approximate
            // one with rather than reintroducing a token no theme can mean.
            std::uint8_t r = 0, g = 0, b = 0;
            ColorToRgb8(color, r, g, b);
            return TrueColorToHex(Color::RGB(r, g, b));
        }
        case Color::Kind::Default:
        default:
            return "default";
    }
}

std::optional<Color> ParseColorToken(std::string_view token) {
    if (token == "default") {
        return Color::Default;
    }
    if (const auto trueColor = ParseHexColor(token)) {
        return trueColor;
    }
    if (token.starts_with("x:")) {
        // Legacy: themes written before ned went truecolor-only (the ANSI
        // fallback pair, and init.janet files from that era) carry palette
        // indices. They still load, but they resolve to
        // real RGB -- nothing puts a palette index back into a theme, since
        // one cannot be composited against or carry alpha.
        const std::string digits(token.substr(2));
        char*             end   = nullptr;
        const long        value = std::strtol(digits.c_str(), &end, 10);
        if (end != digits.c_str() + digits.size() || value < 0 || value > 255) {
            return std::nullopt;
        }
        return LegacyPaletteToRgb(static_cast<int>(value));
    }
    return std::nullopt;
}

// Every colour here is real RGB. The ANSI colour *names* this used to reach
// for (Color::Blue, Color::BrightWhite, ...) stopped meaning "whatever the
// user's terminal calls blue" when the palette-fallback path went away --
// they resolve to xterm's own defaults, which are flatter and darker than
// anything a themed terminal would have shown, so an operator painted in
// #800000 was effectively invisible.
//
// Vibrancy pass: the original set leaned almost entirely on one blue/violet
// family (keyword, number, constant and namespace were all close cousins of
// the same hue), which is what read as monochrome once the desktop-accent
// tint (DesktopThemeProbe.cpp's BuildDesktopTheme) painted the mode line and
// keywords the same purple on top of it. The palette below spreads syntax
// classes across nine distinct, saturated hues -- azure keywords, cyan
// functions, teal-green properties, leaf-green strings, gold types, warm
// orange numbers, rose operators, pink constants, violet namespaces -- so
// the buffer reads as colourful even before any accent tint is applied.
// Each named constant still maps to exactly one colour, so every equality
// the theme expressed (keyword == controlKeyword, function ==
// functionBuiltin, ...) is preserved. The two *background* usages are the
// exception: a colour that reads as text is not a colour text reads on.
Theme DarkTheme() {
    return Theme{
        .name                        = "dark",
        .background                  = Color::Default, // let the terminal's own (typically dark) background show
        .defaultForeground           = Color::RGB(0xcdd6f4),
        .commentForeground           = Color::RGB(0x7f87b3), // slate-lavender, not flat grey -- still recessive once italicized
        .stringForeground            = Color::RGB(0x8fd67e),
        .keywordForeground           = Color::RGB(0x4fb4ff),
        .numberForeground            = Color::RGB(0xff9e64), // warm orange -- freed from the purple family for hue spread
        .docCommentForeground        = Color::RGB(0x9aa0d9),
        .stringEscapeForeground      = Color::RGB(0x7ce0b0), // mint, distinct from stringForeground's leaf green
        .controlKeywordForeground    = Color::RGB(0x4fb4ff),
        .functionForeground          = Color::RGB(0x4fd0e8),
        .functionBuiltinForeground   = Color::RGB(0x4fd0e8),
        .typeForeground              = Color::RGB(0xffcb6b),
        .typeBuiltinForeground       = Color::RGB(0xffcb6b),
        .constantForeground          = Color::RGB(0xff8fd1),
        .constantBuiltinForeground   = Color::RGB(0xff8fd1),
        .variableForeground          = Color::RGB(0xeef0fb),
        .variableBuiltinForeground   = Color::RGB(0xeef0fb),
        .parameterForeground         = Color::RGB(0xffc88a),
        .propertyForeground          = Color::RGB(0x6fe0a0),
        .operatorForeground          = Color::RGB(0xff6b81),
        .punctuationForeground       = Color::RGB(0x7b81ad),
        .tagForeground               = Color::RGB(0x74c7ff),
        .attributeForeground         = Color::RGB(0xffa07a), // coral-orange, kept apart from operator's rose-red
        .namespaceForeground         = Color::RGB(0xc48cff),
        .keywordModifierForeground   = Color::RGB(0x5cb8ff),
        .methodForeground            = Color::RGB(0x4fd0e8),
        .constructorForeground       = Color::RGB(0xe8b96a),
        .labelForeground             = Color::RGB(0xd68fe0),
        .returnTypeForeground        = Color::RGB(0xf0b86a),
        .includePathForeground       = Color::RGB(0xe2a37e),
        .modeLineForeground          = Color::RGB(0xf2f4ff),
        .modeLineGradientStart       = Color::RGB(0x2e2350),
        .modeLineGradientEnd         = Color::RGB(0x1c1638),
        .echoArea                    = Brush{.foreground = Color::RGB(0xffc88a)},
        .lineNumberForeground        = Color::RGB(0x757bab),
        .currentLineNumberForeground = Color::RGB(0xf2f4ff),
        .selectionBackground         = Color::RGB(0x36396e), // deep violet-indigo; keeps ~6:1 against defaultForeground
        .isearchMatchBackground      = Color::RGB(0x6b5822), // warm amber wash, same family as lineInspectBackground
        // Deliberately weaker than the search wash: a bracket match is ambient
        // feedback you glance at, not a result you went looking for.
        .matchingBracketBackground   = Color::RGB(0x35505f),
        .snippetFieldBackground      = Color::RGB(0x423f6e),
        .documentHighlightBackground = Color::RGB(0x275048),
        .lineInspectBackground       = Color::RGB(0x603f16),
        .conflictOursBackground      = Color::RGB(0x2a4a2a), // dim green wash, "mine"
        .conflictTheirsBackground    = Color::RGB(0x2a2a4a), // dim indigo wash, "incoming"
        .conflictBaseBackground      = Color::RGB(0x3a3a3a), // dim neutral gray wash (diff3 only)
        // fg was BrightBlack -- bumped alongside the tab-restyle follow-up
        // so inactive tab labels actually read against their own block now
        // that the blocks are the only chrome on the row.
        .tabBar                        = Brush{.background = Color::RGB(0x1c1638), .foreground = Color::RGB(0xa6a8c9)},
        .activeTab                     = Brush{.background = Color::RGB(0x2e2350), .foreground = Color::RGB(0xf2f4ff), .bold = true},
        .scrollBar                     = Brush{.foreground = Color::RGB(0x757bab)},
        .scrollBarDisabled             = Brush{.foreground = Color::RGB(0x36325a)},
        .binaryForeground              = Color::RGB(0xff5c72),
        .ghostTextForeground           = Color::RGB(0x757bab),
        .linkForeground                = Color::RGB(0x5cc8ff),
        .truncationIndicatorForeground = Color::RGB(0x9f8cff),
        .unsavedChangeIndicator        = Color::RGB(0xe8a76a),
        .diagnosticError               = Color::RGB(0xff6b81),
        .diagnosticWarning             = Color::RGB(0xffcb6b),
        .diagnosticInformation         = Color::RGB(0x4fb4ff),
        .diagnosticHint                = Color::RGB(0x6a70a0),
        .breakpointMarker              = Color::RGB(0xff6b81), // same red family as diagnosticError -- both mean "attention here"
        .executionMarker               = Color::RGB(0xffcb6b), // the conventional debugger yellow
        .executionLineBackground       = Color::RGB(0x3f3a22), // a dim warm wash the yellow arrow reads against
        .unverifiedBreakpointMarker    = Color::RGB(0x6a70a0), // same dim gray as diagnosticHint
        .diffAddedBackground           = Color::RGB(0x2a3a2a), // dim green wash, dark enough to keep default-foreground text legible
        .diffRemovedBackground         = Color::RGB(0x3a2a2a), // dim red wash, same lightness as diffAddedBackground
        .trailingWhitespaceBackground  = Color::RGB(0x40282f), // dim maroon wash, distinct from diffRemovedBackground's red
        .successForeground             = Color::RGB(0x8fd67e), // the theme's own green, matching stringForeground
        .vcsModifiedForeground         = Color::RGB(0x4fb4ff), // the theme's own blue, matching diagnosticInformation
        .vcsUntrackedForeground        = Color::RGB(0x4fd0e8), // cyan: present but unknown to the repository
        .blameRecentForeground         = Color::RGB(0x6fe0a0), // fresh commits read bright...
        .blameOldForeground            = Color::RGB(0x6a70a0), // ...and fade into the hint gray with age
        .indentGuideForeground         = Color::RGB(0x4a4a58), // dim gray, deliberately low-contrast against defaultForeground
        // Depth-colorized-indent-guides follow-up: a 6-color rotation, dim
        // enough to stay secondary to real syntax highlighting (same
        // "deliberately low-contrast" spirit as indentGuideForeground
        // above, just spread across a few distinct hues instead of one).
        .indentGuideDepthPalette  = {Color::RGB(0x9a5555), Color::RGB(0x9a7a55), Color::RGB(0x9a9a55),
                                     Color::RGB(0x559a68), Color::RGB(0x55809a), Color::RGB(0x80559a)},
        .tabGlyphForeground       = Color::RGB(0x4a4a58), // same dim gray as indentGuideForeground
        .headlineLevel1Foreground = Color::RGB(0x74c7ff),
        .headlineLevel2Foreground = Color::RGB(0x6fe0a0),
        .headlineLevel3Foreground = Color::RGB(0x8fd67e),
        .todoKeywordForeground    = Color::RGB(0xff5c72),
        .doneKeywordForeground    = Color::RGB(0x8fd67e),
        .checkboxForeground       = Color::RGB(0xffc88a),
        .underlineForeground      = Color::RGB(0xcdd6f4),
        .strikethroughForeground  = Color::RGB(0x757bab),
        // The chrome family's two poles (chrome-redesign follow-up): border
        // is a quiet structural blue-grey one step lighter than the
        // 0x1c1638/0x2e2350 tab/mode-line chrome it frames; the accent is
        // the same "blurple" truncationIndicatorForeground already uses, so
        // attention-colored chrome stays one hue everywhere. The focused
        // gradient is the base gradient pulled 60% toward that accent --
        // was 35%, bumped after live feedback that the focus signal barely
        // read next to how strongly the resize-drag accent pops --
        // precomputed literals, not Interpolate calls, so a theme file can
        // override the tint independently.
        .border                       = Brush{.foreground = Color::RGB(0x3e3a5e)},
        .borderAccent                 = Brush{.foreground = Color::RGB(0x9f8cff), .bold = true},
        .modeLineFocusedGradientStart = Color::RGB(0x6f5fc0),
        .modeLineFocusedGradientEnd   = Color::RGB(0x6a56b8),
        .markupMarkerForeground       = Color::RGB(0x757bab),
        .diffAddedForeground          = Color::RGB(0x8fd67e), // the theme's own green (stringForeground)
        .diffRemovedForeground        = Color::RGB(0xff6b81), // the diagnosticError/breakpoint red family
        .diffChangedForeground        = Color::RGB(0xffcb6b),
    };
}

// Vibrancy pass (same intent as DarkTheme's own -- see its header comment):
// darker, more saturated versions of the same nine-hue family so the light
// theme reads as colourful rather than pastel-washed-out, while staying
// legible on the cream background. Every hue family lines up with
// DarkTheme's: azure keywords, teal functions, teal-green properties, leaf
// -green strings, gold types, burnt-orange numbers, rose operators, magenta
// constants, violet namespaces.
Theme LightTheme() {
    const Color background = Color::RGB(0xfaf8f2);

    return Theme{
        .name                          = "light",
        .background                    = background,
        .defaultForeground             = Color::RGB(0x202020),
        .commentForeground             = Color::RGB(0x8a86ab), // faded, cool lavender-grey against the cream background
        .stringForeground              = Color::RGB(0x1f8f44),
        .keywordForeground             = Color::RGB(0x1e5fd1),
        .numberForeground              = Color::RGB(0xc4691a), // burnt orange -- freed from the purple family for hue spread
        .docCommentForeground          = Color::RGB(0x9a96b8),
        .stringEscapeForeground        = Color::RGB(0x0f9a7a), // teal-mint, distinct from stringForeground's leaf green
        .controlKeywordForeground      = Color::RGB(0x1e5fd1),
        .functionForeground            = Color::RGB(0x0e8f95),
        .functionBuiltinForeground     = Color::RGB(0x0e8f95),
        .typeForeground                = Color::RGB(0xb07d12),
        .typeBuiltinForeground         = Color::RGB(0xb07d12),
        .constantForeground            = Color::RGB(0xc23a91),
        .constantBuiltinForeground     = Color::RGB(0xc23a91),
        .variableForeground            = Color::RGB(0x2b2b2b),
        .variableBuiltinForeground     = Color::RGB(0x2b2b2b),
        .parameterForeground           = Color::RGB(0xa87a1a),
        .propertyForeground            = Color::RGB(0x158f6a),
        .operatorForeground            = Color::RGB(0xc23b4a),
        .punctuationForeground         = Color::RGB(0x6b6f8f),
        .tagForeground                 = Color::RGB(0x1c66c9),
        .attributeForeground           = Color::RGB(0xd1791a),
        .namespaceForeground           = Color::RGB(0x8438c9),
        .keywordModifierForeground     = Color::RGB(0x2a6fd1),
        .methodForeground              = Color::RGB(0x0e8f95),
        .constructorForeground         = Color::RGB(0xb0821a),
        .labelForeground               = Color::RGB(0xb23a9e),
        .returnTypeForeground          = Color::RGB(0xba7818),
        .includePathForeground         = Color::RGB(0xa8663a),
        .modeLineForeground            = Color::RGB(0xffffff),
        .modeLineGradientStart         = Color::RGB(0x5f6fc7),
        .modeLineGradientEnd           = Color::RGB(0x3f4fa0),
        .echoArea                      = Brush{.background = background, .foreground = Color::RGB(0xa8710a)},
        .lineNumberForeground          = Color::RGB(0x9a9ab8),
        .currentLineNumberForeground   = Color::RGB(0x202020),
        .selectionBackground           = Color::RGB(0xb8c8f5),
        .isearchMatchBackground        = Color::RGB(0xffe58a),
        .matchingBracketBackground     = Color::RGB(0xc8e4ec),
        .snippetFieldBackground        = Color::RGB(0xd0e8c8),
        .documentHighlightBackground   = Color::RGB(0xd8ecec),
        .lineInspectBackground         = Color::RGB(0xf5ddc0),
        .conflictOursBackground        = Color::RGB(0xd8f0d8), // light green wash, "mine"
        .conflictTheirsBackground      = Color::RGB(0xdcdcf5), // light lavender wash, "incoming"
        .conflictBaseBackground        = Color::RGB(0xe8e8e8), // light neutral gray wash (diff3 only)
        .tabBar                        = Brush{.background = Color::RGB(0xe6e1f0), .foreground = Color::RGB(0x72728c)},
        .activeTab                     = Brush{.background = background, .foreground = Color::RGB(0x1c1c2e), .bold = true},
        .scrollBar                     = Brush{.foreground = Color::RGB(0x9a9ab8)},
        .scrollBarDisabled             = Brush{.foreground = Color::RGB(0xdad4e8)},
        .binaryForeground              = Color::RGB(0xc23b4a),
        .ghostTextForeground           = Color::RGB(0x9a9ab8),
        .linkForeground                = Color::RGB(0x1c66c9),
        .truncationIndicatorForeground = Color::RGB(0x6f5fd6),
        .unsavedChangeIndicator        = Color::RGB(0xb0721a),
        .diagnosticError               = Color::RGB(0xc23b4a),
        .diagnosticWarning             = Color::RGB(0xb07d12),
        .diagnosticInformation         = Color::RGB(0x2073c9),
        .diagnosticHint                = Color::RGB(0x8f93b3),
        .breakpointMarker              = Color::RGB(0xc23b4a),
        .executionMarker               = Color::RGB(0xb07d12),
        .executionLineBackground       = Color::RGB(0xf4ecd0),
        .unverifiedBreakpointMarker    = Color::RGB(0x8f93b3), // same muted gray-blue as diagnosticHint
        .diffAddedBackground           = Color::RGB(0xe0f0d8), // light green wash, dark text stays legible
        .diffRemovedBackground         = Color::RGB(0xf5dcdc), // light red wash, same lightness as diffAddedBackground
        .trailingWhitespaceBackground  = Color::RGB(0xf0dde8), // light pink wash, distinct from diffRemovedBackground's red
        .successForeground             = Color::RGB(0x1f8f44), // the theme's own green, matching stringForeground
        .vcsModifiedForeground         = Color::RGB(0x1c66c9), // the theme's own blue, matching linkForeground
        .vcsUntrackedForeground        = Color::RGB(0x0e8f95), // teal: present but unknown to the repository
        .blameRecentForeground         = Color::RGB(0x1a8f6a), // fresh commits read strongest...
        .blameOldForeground            = Color::RGB(0x8f93b3), // ...and fade into the hint gray with age
        .indentGuideForeground         = Color::RGB(0xd8d4e8), // light gray, deliberately low-contrast against defaultForeground
        // Depth-colorized-indent-guides follow-up: DarkTheme's own palette
        // pulled darker/more saturated so each hue stays visible against a
        // light background instead of washing out.
        .indentGuideDepthPalette  = {Color::RGB(0xb03030), Color::RGB(0xb07a20), Color::RGB(0x9a9a20),
                                     Color::RGB(0x2f9a4f), Color::RGB(0x2f70b0), Color::RGB(0x7a2fb0)},
        .tabGlyphForeground       = Color::RGB(0xd8d4e8), // same light gray as indentGuideForeground
        .headlineLevel1Foreground = Color::RGB(0x1c66c9),
        .headlineLevel2Foreground = Color::RGB(0x158f6a),
        .headlineLevel3Foreground = Color::RGB(0x1f8f44),
        .todoKeywordForeground    = Color::RGB(0xb8283a),
        .doneKeywordForeground    = Color::RGB(0x1f8f44),
        .checkboxForeground       = Color::RGB(0xa8710a),
        .underlineForeground      = Color::RGB(0x202020),
        .strikethroughForeground  = Color::RGB(0x8a8aa8),
        // Same two-pole structure as DarkTheme's: a cool structural
        // lavender-grey against the cream background, accent from the
        // mode-line blue-violet family, focused gradient pulled toward the
        // light purple truncationIndicatorForeground uses.
        .border                       = Brush{.background = background, .foreground = Color::RGB(0xc8c2dc)},
        .borderAccent                 = Brush{.background = background, .foreground = Color::RGB(0x5a6fd6), .bold = true},
        .modeLineFocusedGradientStart = Color::RGB(0x6a5fd0),
        .modeLineFocusedGradientEnd   = Color::RGB(0x5650b8),
        .markupMarkerForeground       = Color::RGB(0xa8a2c0),
        .diffAddedForeground          = Color::RGB(0x1f8f44), // the light theme's own string green
        .diffRemovedForeground        = Color::RGB(0xc23b4a),
        .diffChangedForeground        = Color::RGB(0xb07d12),
    };
}

} // namespace ned::ui
