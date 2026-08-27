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
        const std::array<std::uint8_t, 3> channels{c.red, c.green, c.blue};
        std::string                       out = "#000000";
        for (std::size_t i = 0; i < channels.size(); ++i) {
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
        if (token.size() != 7 || token[0] != '#') {
            return std::nullopt;
        }
        const auto r = ParseHexByte(token.substr(1, 2));
        const auto g = ParseHexByte(token.substr(3, 2));
        const auto b = ParseHexByte(token.substr(5, 2));
        if (!r || !g || !b) {
            return std::nullopt;
        }
        return Color::RGB(*r, *g, *b);
    }

} // namespace

// Moved here from ThemeFile.cpp (Janet-configurable-syntax-theme follow-up)
// -- see Theme.h's own doc comment on these two for why.
std::string ColorToToken(const Color& color) {
    switch (color.kind) {
        case Color::Kind::TrueColor:
            return TrueColorToHex(color);
        case Color::Kind::Palette16:
            return "x:" + std::to_string(color.paletteIndex);
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
        const std::string digits(token.substr(2));
        char*             end   = nullptr;
        const long        value = std::strtol(digits.c_str(), &end, 10);
        if (end != digits.c_str() + digits.size() || value < 0 || value > 255) {
            return std::nullopt;
        }
        return Color::Palette(static_cast<std::uint8_t>(value));
    }
    return std::nullopt;
}

Theme DarkTheme() {
    return Theme{
        .name                        = "dark",
        .background                  = Color::Default, // let the terminal's own (typically dark) background show
        .defaultForeground           = Color::White,
        .commentForeground           = Color::RGB(0xa6a6a0),
        .stringForeground            = Color::Green,
        .keywordForeground           = Color::Blue,
        .numberForeground            = Color::Magenta,
        .docCommentForeground        = Color::BrightBlack,
        .stringEscapeForeground      = Color::BrightGreen,
        .controlKeywordForeground    = Color::Blue,
        .functionForeground          = Color::Cyan,
        .functionBuiltinForeground   = Color::Cyan,
        .typeForeground              = Color::Yellow,
        .typeBuiltinForeground       = Color::Yellow,
        .constantForeground          = Color::BrightMagenta,
        .constantBuiltinForeground   = Color::BrightMagenta,
        .variableForeground          = Color::BrightWhite,
        .variableBuiltinForeground   = Color::BrightWhite,
        .parameterForeground         = Color::BrightYellow,
        .propertyForeground          = Color::BrightCyan,
        .operatorForeground          = Color::Red,
        .punctuationForeground       = Color::BrightBlack,
        .tagForeground               = Color::BrightBlue,
        .attributeForeground         = Color::BrightRed,
        .namespaceForeground         = Color::BrightMagenta,
        .keywordModifierForeground   = Color::RGB(0x6fa8dc),
        .methodForeground            = Color::RGB(0x4ec9b0),
        .constructorForeground       = Color::RGB(0xd7ba7d),
        .labelForeground             = Color::RGB(0xc586c0),
        .returnTypeForeground        = Color::RGB(0xe0af68),
        .includePathForeground       = Color::RGB(0xce9178),
        .modeLineForeground          = Color::BrightWhite,
        .modeLineGradientStart       = Color::RGB(0x2b2b40),
        .modeLineGradientEnd         = Color::RGB(0x1b1b30),
        .echoArea                    = Brush{.foreground = Color::BrightYellow},
        .lineNumberForeground        = Color::BrightBlack,
        .currentLineNumberForeground = Color::BrightWhite,
        .selectionBackground         = Color::Blue,
        .isearchMatchBackground      = Color::Yellow,
        .snippetFieldBackground      = Color::RGB(0x3d3d5c),
        .documentHighlightBackground = Color::RGB(0x2a4a4a),
        // fg was BrightBlack -- bumped alongside the tab-restyle follow-up
        // so inactive tab labels actually read against their own block now
        // that the blocks are the only chrome on the row.
        .tabBar                        = Brush{.background = Color::RGB(0x1b1b30), .foreground = Color::RGB(0x9898b0)},
        .activeTab                     = Brush{.background = Color::RGB(0x2b2b40), .foreground = Color::BrightWhite, .bold = true},
        .scrollBar                     = Brush{.foreground = Color::BrightBlack},
        .scrollBarDisabled             = Brush{.foreground = Color::RGB(0x333340)},
        .binaryForeground              = Color::BrightRed,
        .ghostTextForeground           = Color::BrightBlack,
        .linkForeground                = Color::BrightCyan,
        .truncationIndicatorForeground = Color::RGB(0x8f80e0),
        .unsavedChangeIndicator        = Color::RGB(0xd19a66),
        .diagnosticError               = Color::RGB(0xe06c75),
        .diagnosticWarning             = Color::RGB(0xe5c07b),
        .diagnosticInformation         = Color::RGB(0x61afef),
        .diagnosticHint                = Color::RGB(0x5c6370),
        .breakpointMarker              = Color::RGB(0xe06c75), // same red family as diagnosticError -- both mean "attention here"
        .executionMarker               = Color::RGB(0xe5c07b), // the conventional debugger yellow
        .executionLineBackground       = Color::RGB(0x3a3a28), // a dim warm wash the yellow arrow reads against
        .unverifiedBreakpointMarker    = Color::RGB(0x5c6370), // same dim gray as diagnosticHint
        .diffAddedBackground           = Color::RGB(0x2a3a2a), // dim green wash, dark enough to keep default-foreground text legible
        .diffRemovedBackground         = Color::RGB(0x3a2a2a), // dim red wash, same lightness as diffAddedBackground
        .trailingWhitespaceBackground  = Color::RGB(0x40282f), // dim maroon wash, distinct from diffRemovedBackground's red
        .indentGuideForeground         = Color::RGB(0x4a4a48), // dim gray, deliberately low-contrast against defaultForeground
        .headlineLevel1Foreground      = Color::BrightBlue,
        .headlineLevel2Foreground      = Color::BrightCyan,
        .headlineLevel3Foreground      = Color::BrightGreen,
        .todoKeywordForeground         = Color::BrightRed,
        .doneKeywordForeground         = Color::BrightGreen,
        .checkboxForeground            = Color::BrightYellow,
        .underlineForeground           = Color::White,
        .strikethroughForeground       = Color::BrightBlack,
        // The chrome family's two poles (chrome-redesign follow-up): border
        // is a quiet structural blue-grey one step lighter than the
        // 0x1b1b30/0x2b2b40 tab/mode-line chrome it frames; the accent is
        // the same "blurple" truncationIndicatorForeground already uses, so
        // attention-colored chrome stays one hue everywhere. The focused
        // gradient is the base gradient pulled 60% toward that accent --
        // was 35%, bumped after live feedback that the focus signal barely
        // read next to how strongly the resize-drag accent pops --
        // precomputed literals, not Interpolate calls, so a theme file can
        // override the tint independently.
        .border                       = Brush{.foreground = Color::RGB(0x3a3a50)},
        .borderAccent                 = Brush{.foreground = Color::RGB(0x8f80e0), .bold = true},
        .modeLineFocusedGradientStart = Color::RGB(0x675ea0),
        .modeLineFocusedGradientEnd   = Color::RGB(0x605799),
        .markupMarkerForeground       = Color::BrightBlack,
    };
}

Theme LightTheme() {
    const Color background = Color::RGB(0xfaf8f2);

    return Theme{
        .name                          = "light",
        .background                    = background,
        .defaultForeground             = Color::RGB(0x202020),
        .commentForeground             = Color::RGB(0x8f8f80), // a genuinely faded, warm-toned gray against the cream background
        .stringForeground              = Color::RGB(0x2f6f2f),
        .keywordForeground             = Color::RGB(0x1f4fa0),
        .numberForeground              = Color::RGB(0x8f3f8f),
        .docCommentForeground          = Color::RGB(0x8f8f80),
        .stringEscapeForeground        = Color::RGB(0x1f8f1f),
        .controlKeywordForeground      = Color::RGB(0x1f4fa0),
        .functionForeground            = Color::RGB(0x1f7a7a),
        .functionBuiltinForeground     = Color::RGB(0x1f7a7a),
        .typeForeground                = Color::RGB(0xa0701f),
        .typeBuiltinForeground         = Color::RGB(0xa0701f),
        .constantForeground            = Color::RGB(0xa03f7f),
        .constantBuiltinForeground     = Color::RGB(0xa03f7f),
        .variableForeground            = Color::RGB(0x303030),
        .variableBuiltinForeground     = Color::RGB(0x303030),
        .parameterForeground           = Color::RGB(0x8f6f1f),
        .propertyForeground            = Color::RGB(0x1f6f8f),
        .operatorForeground            = Color::RGB(0xa03f2f),
        .punctuationForeground         = Color::RGB(0x808080),
        .tagForeground                 = Color::RGB(0x1f4fa0),
        .attributeForeground           = Color::RGB(0xc06f1f),
        .namespaceForeground           = Color::RGB(0xa03f7f),
        .keywordModifierForeground     = Color::RGB(0x2f6fa0),
        .methodForeground              = Color::RGB(0x1f8f7a),
        .constructorForeground         = Color::RGB(0x9f7a1f),
        .labelForeground               = Color::RGB(0xa03f8f),
        .returnTypeForeground          = Color::RGB(0xb0701f),
        .includePathForeground         = Color::RGB(0x8f5f3f),
        .modeLineForeground            = Color::RGB(0xffffff),
        .modeLineGradientStart         = Color::RGB(0x5f7fa0),
        .modeLineGradientEnd           = Color::RGB(0x3f5f80),
        .echoArea                      = Brush{.background = background, .foreground = Color::RGB(0x8f5f00)},
        .lineNumberForeground          = Color::RGB(0xa0a0a0),
        .currentLineNumberForeground   = Color::RGB(0x202020),
        .selectionBackground           = Color::RGB(0xbcd4f0),
        .isearchMatchBackground        = Color::RGB(0xffe58a),
        .snippetFieldBackground        = Color::RGB(0xd0e8c8),
        .documentHighlightBackground   = Color::RGB(0xd8ecec),
        .tabBar                        = Brush{.background = Color::RGB(0xe4e0d4), .foreground = Color::RGB(0x707070)},
        .activeTab                     = Brush{.background = background, .foreground = Color::RGB(0x202020), .bold = true},
        .scrollBar                     = Brush{.foreground = Color::RGB(0xa0a0a0)},
        .scrollBarDisabled             = Brush{.foreground = Color::RGB(0xd8d4c8)},
        .binaryForeground              = Color::RGB(0xc03030),
        .ghostTextForeground           = Color::RGB(0xa0a0a0),
        .linkForeground                = Color::RGB(0x1f6fa0),
        .truncationIndicatorForeground = Color::RGB(0x6a5acd),
        .unsavedChangeIndicator        = Color::RGB(0xb0651f),
        .diagnosticError               = Color::RGB(0xc0392b),
        .diagnosticWarning             = Color::RGB(0xb58900),
        .diagnosticInformation         = Color::RGB(0x2980b9),
        .diagnosticHint                = Color::RGB(0x95a5a6),
        .breakpointMarker              = Color::RGB(0xc0392b),
        .executionMarker               = Color::RGB(0xb58900),
        .executionLineBackground       = Color::RGB(0xf4ecd0),
        .unverifiedBreakpointMarker    = Color::RGB(0x95a5a6), // same muted gray-blue as diagnosticHint
        .diffAddedBackground           = Color::RGB(0xe0f0d8), // light green wash, dark text stays legible
        .diffRemovedBackground         = Color::RGB(0xf5dcdc), // light red wash, same lightness as diffAddedBackground
        .trailingWhitespaceBackground  = Color::RGB(0xf0dde8), // light pink wash, distinct from diffRemovedBackground's red
        .indentGuideForeground         = Color::RGB(0xd8d8d0), // light gray, deliberately low-contrast against defaultForeground
        .headlineLevel1Foreground      = Color::RGB(0x1f4fa0),
        .headlineLevel2Foreground      = Color::RGB(0x1f7a7a),
        .headlineLevel3Foreground      = Color::RGB(0x2f6f2f),
        .todoKeywordForeground         = Color::RGB(0xa03030),
        .doneKeywordForeground         = Color::RGB(0x2f8f2f),
        .checkboxForeground            = Color::RGB(0x8f6f1f),
        .underlineForeground           = Color::RGB(0x202020),
        .strikethroughForeground       = Color::RGB(0x808080),
        // Same two-pole structure as DarkTheme's: a warm structural grey
        // against the cream background, accent from the mode-line blue
        // family, focused gradient pulled toward the light purple
        // truncationIndicatorForeground uses.
        .border                       = Brush{.background = background, .foreground = Color::RGB(0xc8c4b8)},
        .borderAccent                 = Brush{.background = background, .foreground = Color::RGB(0x5f7fa0), .bold = true},
        .modeLineFocusedGradientStart = Color::RGB(0x6568bb),
        .modeLineFocusedGradientEnd   = Color::RGB(0x585cae),
        .markupMarkerForeground       = Color::RGB(0xa8a496),
    };
}

// ansi-fallback-theme follow-up (see Theme.h's own comment on these two for
// the 0-7-plus-Default restriction and the equal-gradient-endpoints rule).
// Hue choices echo DarkTheme's where it already used palette colors
// (string=Green, keyword=Blue, number=Magenta, operator=Red, type=Yellow,
// selection=Blue, isearch=Yellow); its gray/RGB accents map to whichever of
// the 7 usable hues (Black is unusable against a dark background) reads
// closest in role. Collisions are unavoidable at 8 colors -- the
// deliberately-default-colored categories (variables, punctuation, ...) all
// share White so the load-bearing five (comment/string/keyword/number/type)
// each keep a distinct hue.
Theme AnsiDarkTheme() {
    return Theme{
        .name                          = "ansi-dark",
        .background                    = Color::Default,
        .defaultForeground             = Color::Default,
        .commentForeground             = Color::Cyan,
        .stringForeground              = Color::Green,
        .keywordForeground             = Color::Blue,
        .numberForeground              = Color::Magenta,
        .docCommentForeground          = Color::Cyan,
        .stringEscapeForeground        = Color::Green,
        .controlKeywordForeground      = Color::Blue,
        .functionForeground            = Color::White,
        .functionBuiltinForeground     = Color::White,
        .typeForeground                = Color::Yellow,
        .typeBuiltinForeground         = Color::Yellow,
        .constantForeground            = Color::Magenta,
        .constantBuiltinForeground     = Color::Magenta,
        .variableForeground            = Color::White,
        .variableBuiltinForeground     = Color::White,
        .parameterForeground           = Color::White,
        .propertyForeground            = Color::White,
        .operatorForeground            = Color::Red,
        .punctuationForeground         = Color::White,
        .tagForeground                 = Color::Blue,
        .attributeForeground           = Color::Red,
        .namespaceForeground           = Color::Magenta,
        .keywordModifierForeground     = Color::Blue,
        .methodForeground              = Color::White,
        .constructorForeground         = Color::White,
        .labelForeground               = Color::Magenta,
        .returnTypeForeground          = Color::Yellow,
        .includePathForeground         = Color::Green,
        .modeLineForeground            = Color::White,
        .modeLineGradientStart         = Color::Blue,
        .modeLineGradientEnd           = Color::Blue,
        .echoArea                      = Brush{.foreground = Color::Yellow},
        .lineNumberForeground          = Color::Blue,
        .currentLineNumberForeground   = Color::White,
        .selectionBackground           = Color::Blue,
        .isearchMatchBackground        = Color::Yellow,
        .snippetFieldBackground        = Color::Green,
        .documentHighlightBackground   = Color::Magenta,
        .tabBar                        = Brush{.foreground = Color::White},
        .activeTab                     = Brush{.background = Color::Blue, .foreground = Color::White, .bold = true},
        .scrollBar                     = Brush{.foreground = Color::White},
        .scrollBarDisabled             = Brush{.foreground = Color::Blue},
        .binaryForeground              = Color::Red,
        .ghostTextForeground           = Color::Blue,
        .linkForeground                = Color::Cyan,
        .truncationIndicatorForeground = Color::Magenta,
        .unsavedChangeIndicator        = Color::Yellow,
        .diagnosticError               = Color::Red,
        .diagnosticWarning             = Color::Yellow,
        .diagnosticInformation         = Color::Blue,
        .diagnosticHint                = Color::Cyan,
        .breakpointMarker              = Color::Red,
        .executionMarker               = Color::Yellow,
        .executionLineBackground       = Color::Blue,
        .unverifiedBreakpointMarker    = Color::Cyan, // same as diagnosticHint -- ANSI fallback restricted to palette 0-7, no Bright range
        .diffAddedBackground           = Color::Green,
        .diffRemovedBackground         = Color::Red,
        .trailingWhitespaceBackground  = Color::Yellow,
        .indentGuideForeground         = Color::Blue,
        .headlineLevel1Foreground      = Color::Blue,
        .headlineLevel2Foreground      = Color::Cyan,
        .headlineLevel3Foreground      = Color::Green,
        .todoKeywordForeground         = Color::Red,
        .doneKeywordForeground         = Color::Green,
        .checkboxForeground            = Color::Yellow,
        .underlineForeground           = Color::White,
        .strikethroughForeground       = Color::White,
        .border                        = Brush{.foreground = Color::Blue},
        .borderAccent                  = Brush{.foreground = Color::Magenta, .bold = true},
        .modeLineFocusedGradientStart  = Color::Magenta,
        .modeLineFocusedGradientEnd    = Color::Magenta,
        .markupMarkerForeground        = Color::Blue,
    };
}

// Same structure against a light terminal background. Yellow/Cyan read
// poorly on white but stay in their conventional roles (type, comment) --
// every other choice favors the dark-on-light half of the base palette.
Theme AnsiLightTheme() {
    return Theme{
        .name                          = "ansi-light",
        .background                    = Color::Default,
        .defaultForeground             = Color::Black,
        .commentForeground             = Color::Cyan,
        .stringForeground              = Color::Green,
        .keywordForeground             = Color::Blue,
        .numberForeground              = Color::Magenta,
        .docCommentForeground          = Color::Cyan,
        .stringEscapeForeground        = Color::Green,
        .controlKeywordForeground      = Color::Blue,
        .functionForeground            = Color::Black,
        .functionBuiltinForeground     = Color::Black,
        .typeForeground                = Color::Yellow,
        .typeBuiltinForeground         = Color::Yellow,
        .constantForeground            = Color::Magenta,
        .constantBuiltinForeground     = Color::Magenta,
        .variableForeground            = Color::Black,
        .variableBuiltinForeground     = Color::Black,
        .parameterForeground           = Color::Black,
        .propertyForeground            = Color::Black,
        .operatorForeground            = Color::Red,
        .punctuationForeground         = Color::Black,
        .tagForeground                 = Color::Blue,
        .attributeForeground           = Color::Red,
        .namespaceForeground           = Color::Magenta,
        .keywordModifierForeground     = Color::Blue,
        .methodForeground              = Color::Black,
        .constructorForeground         = Color::Black,
        .labelForeground               = Color::Magenta,
        .returnTypeForeground          = Color::Yellow,
        .includePathForeground         = Color::Green,
        .modeLineForeground            = Color::White,
        .modeLineGradientStart         = Color::Blue,
        .modeLineGradientEnd           = Color::Blue,
        .echoArea                      = Brush{.foreground = Color::Blue},
        .lineNumberForeground          = Color::Cyan,
        .currentLineNumberForeground   = Color::Black,
        .selectionBackground           = Color::Cyan,
        .isearchMatchBackground        = Color::Yellow,
        .snippetFieldBackground        = Color::Green,
        .documentHighlightBackground   = Color::Magenta,
        .tabBar                        = Brush{.foreground = Color::Black},
        .activeTab                     = Brush{.background = Color::Cyan, .foreground = Color::Black, .bold = true},
        .scrollBar                     = Brush{.foreground = Color::Black},
        .scrollBarDisabled             = Brush{.foreground = Color::Cyan},
        .binaryForeground              = Color::Red,
        .ghostTextForeground           = Color::Cyan,
        .linkForeground                = Color::Blue,
        .truncationIndicatorForeground = Color::Magenta,
        .unsavedChangeIndicator        = Color::Yellow,
        .diagnosticError               = Color::Red,
        .diagnosticWarning             = Color::Yellow,
        .diagnosticInformation         = Color::Blue,
        .diagnosticHint                = Color::Cyan,
        .breakpointMarker              = Color::Red,
        .executionMarker               = Color::Yellow,
        .executionLineBackground       = Color::Cyan,
        .unverifiedBreakpointMarker    = Color::Cyan, // same as diagnosticHint -- ANSI fallback restricted to palette 0-7, no Bright range
        .diffAddedBackground           = Color::Green,
        .diffRemovedBackground         = Color::Red,
        .trailingWhitespaceBackground  = Color::Yellow,
        .indentGuideForeground         = Color::Blue,
        .headlineLevel1Foreground      = Color::Blue,
        .headlineLevel2Foreground      = Color::Magenta,
        .headlineLevel3Foreground      = Color::Green,
        .todoKeywordForeground         = Color::Red,
        .doneKeywordForeground         = Color::Green,
        .checkboxForeground            = Color::Cyan,
        .underlineForeground           = Color::Black,
        .strikethroughForeground       = Color::Black,
        .border                        = Brush{.foreground = Color::Cyan},
        .borderAccent                  = Brush{.foreground = Color::Magenta, .bold = true},
        .modeLineFocusedGradientStart  = Color::Magenta,
        .modeLineFocusedGradientEnd    = Color::Magenta,
        .markupMarkerForeground        = Color::Cyan,
    };
}

Theme AnsiFallbackFor(const Theme& theme) {
    if (theme.background.kind == Color::Kind::TrueColor) {
        // Rec. 601 luma, integer arithmetic -- the midpoint split only needs
        // to separate "genuinely light background" from everything else.
        const int luma =
            (299 * theme.background.red + 587 * theme.background.green + 114 * theme.background.blue) / 1000;
        if (luma >= 128) {
            return AnsiLightTheme();
        }
    }
    return AnsiDarkTheme();
}

} // namespace ned::ui
