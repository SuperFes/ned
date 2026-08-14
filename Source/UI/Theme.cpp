#include "Theme.h"

namespace ned::ui {

ox::Brush Theme::BrushFor(editor::SyntaxClass cls) const {
    switch (cls) {
        case editor::SyntaxClass::Comment:
            return ox::Brush{.background = background, .foreground = commentForeground};
        case editor::SyntaxClass::DocComment:
            return ox::Brush{.background = background, .foreground = docCommentForeground, .traits = ox::Trait::Italic};
        case editor::SyntaxClass::String:
            return ox::Brush{.background = background, .foreground = stringForeground};
        case editor::SyntaxClass::StringEscape:
            return ox::Brush{.background = background, .foreground = stringEscapeForeground};
        case editor::SyntaxClass::Keyword:
            return ox::Brush{.background = background, .foreground = keywordForeground, .traits = ox::Trait::Bold};
        case editor::SyntaxClass::ControlKeyword:
            return ox::Brush{
                .background = background, .foreground = controlKeywordForeground, .traits = ox::Trait::Bold | ox::Trait::Italic};
        case editor::SyntaxClass::Number:
            return ox::Brush{.background = background, .foreground = numberForeground};
        case editor::SyntaxClass::Function:
            return ox::Brush{.background = background, .foreground = functionForeground};
        case editor::SyntaxClass::FunctionBuiltin:
            return ox::Brush{.background = background, .foreground = functionBuiltinForeground, .traits = ox::Trait::Bold};
        case editor::SyntaxClass::Type:
            return ox::Brush{.background = background, .foreground = typeForeground};
        case editor::SyntaxClass::TypeBuiltin:
            return ox::Brush{.background = background, .foreground = typeBuiltinForeground, .traits = ox::Trait::Bold};
        case editor::SyntaxClass::Constant:
            return ox::Brush{.background = background, .foreground = constantForeground};
        case editor::SyntaxClass::ConstantBuiltin:
            return ox::Brush{.background = background, .foreground = constantBuiltinForeground, .traits = ox::Trait::Bold};
        case editor::SyntaxClass::Variable:
            return ox::Brush{.background = background, .foreground = variableForeground};
        case editor::SyntaxClass::VariableBuiltin:
            return ox::Brush{.background = background, .foreground = variableBuiltinForeground, .traits = ox::Trait::Italic};
        case editor::SyntaxClass::Parameter:
            return ox::Brush{.background = background, .foreground = parameterForeground, .traits = ox::Trait::Italic};
        case editor::SyntaxClass::Property:
            return ox::Brush{.background = background, .foreground = propertyForeground};
        case editor::SyntaxClass::Operator:
            return ox::Brush{.background = background, .foreground = operatorForeground};
        case editor::SyntaxClass::Punctuation:
            return ox::Brush{.background = background, .foreground = punctuationForeground};
        case editor::SyntaxClass::Tag:
            return ox::Brush{.background = background, .foreground = tagForeground, .traits = ox::Trait::Bold};
        case editor::SyntaxClass::Attribute:
            return ox::Brush{.background = background, .foreground = attributeForeground};
        case editor::SyntaxClass::Namespace:
            return ox::Brush{.background = background, .foreground = namespaceForeground, .traits = ox::Trait::Italic};
        case editor::SyntaxClass::Default:
        default:
            return ox::Brush{.background = background, .foreground = defaultForeground};
    }
}

Theme DarkTheme() {
    return Theme{
        .name                        = "dark",
        .background                  = ox::TermColor::Default, // let the terminal's own (typically dark) background show
        .defaultForeground           = ox::XColor::White,
        .commentForeground           = ox::XColor::BrightBlack,
        .stringForeground            = ox::XColor::Green,
        .keywordForeground           = ox::XColor::Blue,
        .numberForeground            = ox::XColor::Magenta,
        .docCommentForeground        = ox::XColor::BrightBlack,
        .stringEscapeForeground      = ox::XColor::BrightGreen,
        .controlKeywordForeground    = ox::XColor::Blue,
        .functionForeground          = ox::XColor::Cyan,
        .functionBuiltinForeground   = ox::XColor::Cyan,
        .typeForeground              = ox::XColor::Yellow,
        .typeBuiltinForeground       = ox::XColor::Yellow,
        .constantForeground          = ox::XColor::BrightMagenta,
        .constantBuiltinForeground   = ox::XColor::BrightMagenta,
        .variableForeground          = ox::XColor::BrightWhite,
        .variableBuiltinForeground   = ox::XColor::BrightWhite,
        .parameterForeground         = ox::XColor::BrightYellow,
        .propertyForeground          = ox::XColor::BrightCyan,
        .operatorForeground          = ox::XColor::Red,
        .punctuationForeground       = ox::XColor::BrightBlack,
        .tagForeground               = ox::XColor::BrightBlue,
        .attributeForeground         = ox::XColor::BrightRed,
        .namespaceForeground         = ox::XColor::BrightMagenta,
        .modeLineForeground          = ox::XColor::BrightWhite,
        .modeLineGradientStart       = ox::TrueColor{ox::RGB{0x2b2b40}},
        .modeLineGradientEnd         = ox::TrueColor{ox::RGB{0x1b1b30}},
        .echoArea                    = ox::Brush{.foreground = ox::XColor::BrightYellow},
        .lineNumberForeground        = ox::XColor::BrightBlack,
        .currentLineNumberForeground = ox::XColor::BrightWhite,
        .selectionBackground         = ox::XColor::Blue,
        .isearchMatchBackground      = ox::XColor::Yellow,
        .tabBar                      = ox::Brush{.background = ox::TrueColor{ox::RGB{0x1b1b30}}, .foreground = ox::XColor::BrightBlack},
        .activeTab                   = ox::Brush{.background = ox::TrueColor{ox::RGB{0x2b2b40}}, .foreground = ox::XColor::BrightWhite, .traits = ox::Trait::Bold},
        .scrollBar                   = ox::Brush{.foreground = ox::XColor::BrightBlack},
        .scrollBarDisabled           = ox::Brush{.foreground = ox::TrueColor{ox::RGB{0x333340}}},
        .binaryForeground            = ox::XColor::BrightRed,
    };
}

Theme LightTheme() {
    const ox::TrueColor background = ox::RGB{0xfaf8f2};

    return Theme{
        .name                        = "light",
        .background                  = background,
        .defaultForeground           = ox::TrueColor{ox::RGB{0x202020}},
        .commentForeground           = ox::TrueColor{ox::RGB{0x707070}},
        .stringForeground            = ox::TrueColor{ox::RGB{0x2f6f2f}},
        .keywordForeground           = ox::TrueColor{ox::RGB{0x1f4fa0}},
        .numberForeground            = ox::TrueColor{ox::RGB{0x8f3f8f}},
        .docCommentForeground        = ox::TrueColor{ox::RGB{0x707070}},
        .stringEscapeForeground      = ox::TrueColor{ox::RGB{0x1f8f1f}},
        .controlKeywordForeground    = ox::TrueColor{ox::RGB{0x1f4fa0}},
        .functionForeground          = ox::TrueColor{ox::RGB{0x1f7a7a}},
        .functionBuiltinForeground   = ox::TrueColor{ox::RGB{0x1f7a7a}},
        .typeForeground              = ox::TrueColor{ox::RGB{0xa0701f}},
        .typeBuiltinForeground       = ox::TrueColor{ox::RGB{0xa0701f}},
        .constantForeground          = ox::TrueColor{ox::RGB{0xa03f7f}},
        .constantBuiltinForeground   = ox::TrueColor{ox::RGB{0xa03f7f}},
        .variableForeground          = ox::TrueColor{ox::RGB{0x303030}},
        .variableBuiltinForeground   = ox::TrueColor{ox::RGB{0x303030}},
        .parameterForeground         = ox::TrueColor{ox::RGB{0x8f6f1f}},
        .propertyForeground          = ox::TrueColor{ox::RGB{0x1f6f8f}},
        .operatorForeground          = ox::TrueColor{ox::RGB{0xa03f2f}},
        .punctuationForeground       = ox::TrueColor{ox::RGB{0x808080}},
        .tagForeground               = ox::TrueColor{ox::RGB{0x1f4fa0}},
        .attributeForeground         = ox::TrueColor{ox::RGB{0xc06f1f}},
        .namespaceForeground         = ox::TrueColor{ox::RGB{0xa03f7f}},
        .modeLineForeground          = ox::TrueColor{ox::RGB{0xffffff}},
        .modeLineGradientStart       = ox::TrueColor{ox::RGB{0x5f7fa0}},
        .modeLineGradientEnd         = ox::TrueColor{ox::RGB{0x3f5f80}},
        .echoArea                    = ox::Brush{.background = background, .foreground = ox::TrueColor{ox::RGB{0x8f5f00}}},
        .lineNumberForeground        = ox::TrueColor{ox::RGB{0xa0a0a0}},
        .currentLineNumberForeground = ox::TrueColor{ox::RGB{0x202020}},
        .selectionBackground         = ox::TrueColor{ox::RGB{0xbcd4f0}},
        .isearchMatchBackground      = ox::TrueColor{ox::RGB{0xffe58a}},
        .tabBar                      = ox::Brush{.background = ox::TrueColor{ox::RGB{0xe4e0d4}}, .foreground = ox::TrueColor{ox::RGB{0x707070}}},
        .activeTab                   = ox::Brush{.background = background, .foreground = ox::TrueColor{ox::RGB{0x202020}}, .traits = ox::Trait::Bold},
        .scrollBar                   = ox::Brush{.foreground = ox::TrueColor{ox::RGB{0xa0a0a0}}},
        .scrollBarDisabled           = ox::Brush{.foreground = ox::TrueColor{ox::RGB{0xd8d4c8}}},
        .binaryForeground            = ox::TrueColor{ox::RGB{0xc03030}},
    };
}

} // namespace ned::ui
