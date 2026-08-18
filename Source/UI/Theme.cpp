#include "Theme.h"

#include <array>
#include <cstdlib>

#include "Editor/SyntaxTheme.h"

namespace ned::ui {

ftxui::Color Color::ToFtxui() const {
    switch (kind) {
        case Kind::Default:
            return ftxui::Color::Default;
        case Kind::Palette16:
            return ftxui::Color(static_cast<ftxui::Color::Palette16>(paletteIndex));
        case Kind::TrueColor:
            return ftxui::Color::RGB(red, green, blue);
    }
    return ftxui::Color::Default;
}

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
Brush Theme::BrushFor(editor::SyntaxClass cls) const {
    Brush brush = BuiltinBrushFor(cls);

    const editor::SyntaxStyleOverride override = editor::SyntaxOverrideFor(cls);
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
        .tabBar                      = Brush{.background = Color::RGB(0x1b1b30), .foreground = Color::BrightBlack},
        .activeTab                   = Brush{.background = Color::RGB(0x2b2b40), .foreground = Color::BrightWhite, .bold = true},
        .scrollBar                   = Brush{.foreground = Color::BrightBlack},
        .scrollBarDisabled           = Brush{.foreground = Color::RGB(0x333340)},
        .binaryForeground            = Color::BrightRed,
        .linkForeground              = Color::BrightCyan,
        .unsavedChangeIndicator      = Color::RGB(0xd19a66),
        .headlineLevel1Foreground    = Color::BrightBlue,
        .headlineLevel2Foreground    = Color::BrightCyan,
        .headlineLevel3Foreground    = Color::BrightGreen,
        .todoKeywordForeground       = Color::BrightRed,
        .doneKeywordForeground       = Color::BrightGreen,
        .checkboxForeground          = Color::BrightYellow,
        .underlineForeground         = Color::White,
        .strikethroughForeground     = Color::BrightBlack,
    };
}

Theme LightTheme() {
    const Color background = Color::RGB(0xfaf8f2);

    return Theme{
        .name                        = "light",
        .background                  = background,
        .defaultForeground           = Color::RGB(0x202020),
        .commentForeground           = Color::RGB(0x8f8f80), // a genuinely faded, warm-toned gray against the cream background
        .stringForeground            = Color::RGB(0x2f6f2f),
        .keywordForeground           = Color::RGB(0x1f4fa0),
        .numberForeground            = Color::RGB(0x8f3f8f),
        .docCommentForeground        = Color::RGB(0x8f8f80),
        .stringEscapeForeground      = Color::RGB(0x1f8f1f),
        .controlKeywordForeground    = Color::RGB(0x1f4fa0),
        .functionForeground          = Color::RGB(0x1f7a7a),
        .functionBuiltinForeground   = Color::RGB(0x1f7a7a),
        .typeForeground              = Color::RGB(0xa0701f),
        .typeBuiltinForeground       = Color::RGB(0xa0701f),
        .constantForeground          = Color::RGB(0xa03f7f),
        .constantBuiltinForeground   = Color::RGB(0xa03f7f),
        .variableForeground          = Color::RGB(0x303030),
        .variableBuiltinForeground   = Color::RGB(0x303030),
        .parameterForeground         = Color::RGB(0x8f6f1f),
        .propertyForeground          = Color::RGB(0x1f6f8f),
        .operatorForeground          = Color::RGB(0xa03f2f),
        .punctuationForeground       = Color::RGB(0x808080),
        .tagForeground               = Color::RGB(0x1f4fa0),
        .attributeForeground         = Color::RGB(0xc06f1f),
        .namespaceForeground         = Color::RGB(0xa03f7f),
        .keywordModifierForeground   = Color::RGB(0x2f6fa0),
        .methodForeground            = Color::RGB(0x1f8f7a),
        .constructorForeground       = Color::RGB(0x9f7a1f),
        .labelForeground             = Color::RGB(0xa03f8f),
        .returnTypeForeground        = Color::RGB(0xb0701f),
        .includePathForeground       = Color::RGB(0x8f5f3f),
        .modeLineForeground          = Color::RGB(0xffffff),
        .modeLineGradientStart       = Color::RGB(0x5f7fa0),
        .modeLineGradientEnd         = Color::RGB(0x3f5f80),
        .echoArea                    = Brush{.background = background, .foreground = Color::RGB(0x8f5f00)},
        .lineNumberForeground        = Color::RGB(0xa0a0a0),
        .currentLineNumberForeground = Color::RGB(0x202020),
        .selectionBackground         = Color::RGB(0xbcd4f0),
        .isearchMatchBackground      = Color::RGB(0xffe58a),
        .tabBar                      = Brush{.background = Color::RGB(0xe4e0d4), .foreground = Color::RGB(0x707070)},
        .activeTab                   = Brush{.background = background, .foreground = Color::RGB(0x202020), .bold = true},
        .scrollBar                   = Brush{.foreground = Color::RGB(0xa0a0a0)},
        .scrollBarDisabled           = Brush{.foreground = Color::RGB(0xd8d4c8)},
        .binaryForeground            = Color::RGB(0xc03030),
        .linkForeground              = Color::RGB(0x1f6fa0),
        .unsavedChangeIndicator      = Color::RGB(0xb0651f),
        .headlineLevel1Foreground    = Color::RGB(0x1f4fa0),
        .headlineLevel2Foreground    = Color::RGB(0x1f7a7a),
        .headlineLevel3Foreground    = Color::RGB(0x2f6f2f),
        .todoKeywordForeground       = Color::RGB(0xa03030),
        .doneKeywordForeground       = Color::RGB(0x2f8f2f),
        .checkboxForeground          = Color::RGB(0x8f6f1f),
        .underlineForeground         = Color::RGB(0x202020),
        .strikethroughForeground     = Color::RGB(0x808080),
    };
}

} // namespace ned::ui
