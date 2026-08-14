#include "Theme.h"

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

Brush Theme::BrushFor(editor::SyntaxClass cls) const {
    switch (cls) {
        case editor::SyntaxClass::Comment:
            return Brush{.background = background, .foreground = commentForeground};
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
        case editor::SyntaxClass::Default:
        default:
            return Brush{.background = background, .foreground = defaultForeground};
    }
}

Theme DarkTheme() {
    return Theme{
        .name                        = "dark",
        .background                  = Color::Default, // let the terminal's own (typically dark) background show
        .defaultForeground           = Color::White,
        .commentForeground           = Color::BrightBlack,
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
    };
}

Theme LightTheme() {
    const Color background = Color::RGB(0xfaf8f2);

    return Theme{
        .name                        = "light",
        .background                  = background,
        .defaultForeground           = Color::RGB(0x202020),
        .commentForeground           = Color::RGB(0x707070),
        .stringForeground            = Color::RGB(0x2f6f2f),
        .keywordForeground           = Color::RGB(0x1f4fa0),
        .numberForeground            = Color::RGB(0x8f3f8f),
        .docCommentForeground        = Color::RGB(0x707070),
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
    };
}

} // namespace ned::ui
