#include "ThemePalette.h"

#include <utility>

namespace ned::ui {

// The one palette-slot -> Theme-field role mapping (see ThemePalette.h's
// header comment). Role choices mirror DarkTheme's own so a derived theme
// reads like the built-ins with the hues swapped: string=green,
// keyword=blue, number=magenta, constant/namespace/label=purple,
// function=cyan, type=yellow, operator=red, and the orange family carries
// the "warm annotation" roles (parameters' return types, include paths,
// attributes, the unsaved-change marker). Trait differentiation (bold
// builtins, italic parameters, ...) is BrushFor()'s job, not repeated here.
Theme ThemeFromPalette(std::string name, const ThemePalette& p) {
    // Inactive tab-bar text sits between the chrome's own two poles --
    // DarkTheme's 0x9898b0 against BrightWhite/0x1b1b30 is roughly this
    // blend, now derived instead of hand-picked per theme.
    const Color dimChromeForeground = Color::Interpolate(0.4F, p.chromeForeground, p.chromeBackground);

    // A brighter same-family variant for the two "escape/emphasis within a
    // colored span" roles DarkTheme gives Bright* palette entries
    // (stringEscape vs. string, property vs. function) -- pulled toward the
    // plain foreground rather than toward white so it works for light
    // palettes too.
    const Color stringEscape = Color::Interpolate(0.35F, p.green, p.foreground);
    const Color property     = Color::Interpolate(0.35F, p.cyan, p.foreground);

    return Theme{
        .name                          = std::move(name),
        .background                    = p.background,
        .defaultForeground             = p.foreground,
        .commentForeground             = p.subtleForeground,
        .stringForeground              = p.green,
        .keywordForeground             = p.blue,
        .numberForeground              = p.magenta,
        .docCommentForeground          = p.subtleForeground,
        .stringEscapeForeground        = stringEscape,
        .controlKeywordForeground      = p.blue,
        .functionForeground            = p.cyan,
        .functionBuiltinForeground     = p.cyan,
        .typeForeground                = p.yellow,
        .typeBuiltinForeground         = p.yellow,
        .constantForeground            = p.purple,
        .constantBuiltinForeground     = p.purple,
        .variableForeground            = p.foreground,
        .variableBuiltinForeground     = p.foreground,
        .parameterForeground           = p.yellow,
        .propertyForeground            = property,
        .operatorForeground            = p.red,
        .punctuationForeground         = p.subtleForeground,
        .tagForeground                 = p.blue,
        .attributeForeground           = p.orange,
        .namespaceForeground           = p.purple,
        .keywordModifierForeground     = p.blue,
        .methodForeground              = p.cyan,
        .constructorForeground         = p.yellow,
        .labelForeground               = p.purple,
        .returnTypeForeground          = p.orange,
        .includePathForeground         = p.orange,
        .modeLineForeground            = p.chromeForeground,
        .modeLineGradientStart         = p.chromeBackgroundEmphasis,
        .modeLineGradientEnd           = p.chromeBackground,
        .echoArea                      = Brush{.background = p.background, .foreground = p.yellow},
        .lineNumberForeground          = p.subtleForeground,
        .currentLineNumberForeground   = p.foreground,
        .selectionBackground           = p.selectionBackground,
        .isearchMatchBackground        = p.searchMatchBackground,
        .snippetFieldBackground        = p.selectionBackground,
        .tabBar                        = Brush{.background = p.chromeBackground, .foreground = dimChromeForeground},
        .activeTab                     = Brush{.background = p.chromeBackgroundEmphasis, .foreground = p.chromeForeground, .bold = true},
        .scrollBar                     = Brush{.foreground = p.subtleForeground},
        .scrollBarDisabled             = Brush{.foreground = Color::Interpolate(0.6F, p.subtleForeground, p.background)},
        .binaryForeground              = p.red,
        .ghostTextForeground           = p.subtleForeground,
        .linkForeground                = p.cyan,
        .truncationIndicatorForeground = p.accent,
        .unsavedChangeIndicator        = p.orange,
        .diagnosticError               = p.red,
        .diagnosticWarning             = p.yellow,
        .diagnosticInformation         = p.blue,
        .diagnosticHint                = p.subtleForeground,
        .breakpointMarker              = p.red,
        .executionMarker               = p.yellow,
        // A faint warm wash the yellow execution arrow reads against --
        // mostly background, same intent as DarkTheme's own 0x3a3a28.
        .executionLineBackground = Color::Interpolate(0.85F, p.yellow, p.background),
        // Multibuffers follow-up: same mostly-background wash technique as
        // executionLineBackground above, tinted by green/red instead of
        // yellow -- the *vcs diff* multibuffer's added/removed line
        // backgrounds.
        .diffAddedBackground   = Color::Interpolate(0.82F, p.green, p.background),
        .diffRemovedBackground = Color::Interpolate(0.82F, p.red, p.background),
        // Whitespace-visualization follow-up: same mostly-background-wash
        // technique, tinted by magenta (distinct from diff's green/red pair)
        // for trailing whitespace; the indent guide is a faint line-toward-
        // background blend of subtleForeground, the same relationship
        // scrollBarDisabled above already uses for "visible but recessive."
        .trailingWhitespaceBackground = Color::Interpolate(0.82F, p.magenta, p.background),
        .indentGuideForeground        = Color::Interpolate(0.5F, p.subtleForeground, p.background),
        .headlineLevel1Foreground     = p.blue,
        .headlineLevel2Foreground     = p.cyan,
        .headlineLevel3Foreground     = p.green,
        .todoKeywordForeground        = p.red,
        .doneKeywordForeground        = p.green,
        .checkboxForeground           = p.yellow,
        .underlineForeground          = p.foreground,
        .strikethroughForeground      = p.subtleForeground,
        .border                       = Brush{.background = p.background, .foreground = p.border},
        .borderAccent                 = Brush{.background = p.background, .foreground = p.accent, .bold = true},
        // The base gradient pulled 60% toward the accent -- the exact blend
        // DarkTheme's own focused-gradient comment documents as hand-derived
        // literals there, computed here instead.
        .modeLineFocusedGradientStart = Color::Interpolate(0.6F, p.chromeBackgroundEmphasis, p.accent),
        .modeLineFocusedGradientEnd   = Color::Interpolate(0.6F, p.chromeBackground, p.accent),
        .markupMarkerForeground       = p.subtleForeground,
    };
}

} // namespace ned::ui
