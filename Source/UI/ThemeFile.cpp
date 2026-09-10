#include "ThemeFile.h"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>

#include "Editor/SyntaxTheme.h"
#include "PaintParse.h"
#include "ThemePaints.h"

namespace ned::ui {

namespace {

    // The one key<->field table SerializeThemeJanet and SetThemeColorByKey walk
    // (theme-editing follow-up) -- replaces the old hand-mirrored pair of a
    // 50-line serializer and a 50-branch parser, which had silently drifted:
    // every per-SyntaxClass color added since the bundle-remaining-grammars
    // follow-up (function/type/constant/variable/..., 19 fields) was never
    // serialized at all, fine for the chrome-only theme cache that used to
    // be the other consumer but fatal for a theme written as ned/theme-set
    // calls, where an omitted field is simply unsettable. A shared table makes that drift structurally impossible:
    // a field is either here (serialized AND parsed) or not.
    //
    // Key names are the file format -- existing keys must never be renamed
    // (an existing init.janet keeps working; an unrecognized key
    // is reported once and ignored rather than being an error).
    struct ThemeColorKey {
        std::string_view key;
        ui::Color ui::Theme::* field;
    };

    constexpr ThemeColorKey kColorKeys[] = {
        {"background", &Theme::background},
        {"default_foreground", &Theme::defaultForeground},
        {"comment_foreground", &Theme::commentForeground},
        {"doc_comment_foreground", &Theme::docCommentForeground},
        {"string_foreground", &Theme::stringForeground},
        {"string_escape_foreground", &Theme::stringEscapeForeground},
        {"keyword_foreground", &Theme::keywordForeground},
        {"control_keyword_foreground", &Theme::controlKeywordForeground},
        {"keyword_modifier_foreground", &Theme::keywordModifierForeground},
        {"number_foreground", &Theme::numberForeground},
        {"function_foreground", &Theme::functionForeground},
        {"function_builtin_foreground", &Theme::functionBuiltinForeground},
        {"method_foreground", &Theme::methodForeground},
        {"constructor_foreground", &Theme::constructorForeground},
        {"type_foreground", &Theme::typeForeground},
        {"type_builtin_foreground", &Theme::typeBuiltinForeground},
        {"return_type_foreground", &Theme::returnTypeForeground},
        {"constant_foreground", &Theme::constantForeground},
        {"constant_builtin_foreground", &Theme::constantBuiltinForeground},
        {"variable_foreground", &Theme::variableForeground},
        {"variable_builtin_foreground", &Theme::variableBuiltinForeground},
        {"parameter_foreground", &Theme::parameterForeground},
        {"property_foreground", &Theme::propertyForeground},
        {"operator_foreground", &Theme::operatorForeground},
        {"punctuation_foreground", &Theme::punctuationForeground},
        {"tag_foreground", &Theme::tagForeground},
        {"attribute_foreground", &Theme::attributeForeground},
        {"namespace_foreground", &Theme::namespaceForeground},
        {"label_foreground", &Theme::labelForeground},
        {"include_path_foreground", &Theme::includePathForeground},
        {"markup_marker_foreground", &Theme::markupMarkerForeground},
        {"mode_line_foreground", &Theme::modeLineForeground},
        // Gradient endpoints accept any color token now, not hex-only as
        // originally documented: theme colours are truecolor or Default,
        // and a legacy "x:<n>" token from a pre-truecolor file resolves to
        // real RGB on the way in (Theme.cpp's ParseColorToken).
        {"mode_line_gradient_start", &Theme::modeLineGradientStart},
        {"mode_line_gradient_end", &Theme::modeLineGradientEnd},
        {"mode_line_focused_gradient_start", &Theme::modeLineFocusedGradientStart},
        {"mode_line_focused_gradient_end", &Theme::modeLineFocusedGradientEnd},
        {"line_number_foreground", &Theme::lineNumberForeground},
        {"current_line_number_foreground", &Theme::currentLineNumberForeground},
        {"selection_background", &Theme::selectionBackground},
        {"isearch_match_background", &Theme::isearchMatchBackground},
        {"snippet_field_background", &Theme::snippetFieldBackground},
        {"document_highlight_background", &Theme::documentHighlightBackground},
        {"conflict_ours_background", &Theme::conflictOursBackground},
        {"conflict_theirs_background", &Theme::conflictTheirsBackground},
        {"conflict_base_background", &Theme::conflictBaseBackground},
        {"binary_foreground", &Theme::binaryForeground},
        {"ghost_text_foreground", &Theme::ghostTextForeground},
        {"link_foreground", &Theme::linkForeground},
        {"truncation_indicator_foreground", &Theme::truncationIndicatorForeground},
        {"unsaved_change_indicator", &Theme::unsavedChangeIndicator},
        {"diagnostic_error", &Theme::diagnosticError},
        {"diagnostic_warning", &Theme::diagnosticWarning},
        {"diagnostic_information", &Theme::diagnosticInformation},
        {"diagnostic_hint", &Theme::diagnosticHint},
        {"breakpoint_marker", &Theme::breakpointMarker},
        {"execution_marker", &Theme::executionMarker},
        {"unverified_breakpoint_marker", &Theme::unverifiedBreakpointMarker},
        {"execution_line_background", &Theme::executionLineBackground},
        {"diff_added_background", &Theme::diffAddedBackground},
        {"diff_removed_background", &Theme::diffRemovedBackground},
        {"trailing_whitespace_background", &Theme::trailingWhitespaceBackground},
        {"success_foreground", &Theme::successForeground},
        {"vcs_modified_foreground", &Theme::vcsModifiedForeground},
        {"vcs_untracked_foreground", &Theme::vcsUntrackedForeground},
        {"blame_recent_foreground", &Theme::blameRecentForeground},
        {"blame_old_foreground", &Theme::blameOldForeground},
        {"indent_guide_foreground", &Theme::indentGuideForeground},
        {"headline_level1_foreground", &Theme::headlineLevel1Foreground},
        {"headline_level2_foreground", &Theme::headlineLevel2Foreground},
        {"headline_level3_foreground", &Theme::headlineLevel3Foreground},
        {"todo_keyword_foreground", &Theme::todoKeywordForeground},
        {"done_keyword_foreground", &Theme::doneKeywordForeground},
        {"checkbox_foreground", &Theme::checkboxForeground},
        {"underline_foreground", &Theme::underlineForeground},
        {"strikethrough_foreground", &Theme::strikethroughForeground},
    };

    // Brush-valued fields serialize as <prefix>_background/<prefix>_foreground
    // color pairs plus <prefix>_bold/_italic/_underlined/_strikethrough trait
    // flags (bold/italic-round-trip follow-up -- closes the long-standing gap
    // this table used to have: only background/foreground ever persisted,
    // silently dropping e.g. activeTab's own bold, which left it
    // unsettable from a theme).
    struct ThemeBrushKey {
        std::string_view prefix;
        ui::Brush ui::Theme::* field;
    };

    constexpr ThemeBrushKey kBrushKeys[] = {
        {"echo_area", &Theme::echoArea},
        {"tab_bar", &Theme::tabBar},
        {"active_tab", &Theme::activeTab},
        {"scroll_bar", &Theme::scrollBar},
        {"scroll_bar_disabled", &Theme::scrollBarDisabled},
        {"border", &Theme::border},
        {"border_accent", &Theme::borderAccent},
    };

    // "true"/"false" only -- deliberately not case-insensitive or accepting
    // any other spelling, matching ParseColorToken's own strict "the format
    // this file itself writes, nothing looser" precedent.
    std::string BoolToken(bool value) {
        return value ? "true" : "false";
    }

    std::optional<bool> ParseBoolToken(std::string_view token) {
        if (token == "true") {
            return true;
        }
        if (token == "false") {
            return false;
        }
        return std::nullopt;
    }

} // namespace

namespace {

    // The four trait suffixes, in the order every key listing and every
    // Brush walk uses them.
    constexpr std::string_view kTraitSuffixes[] = {"_bold", "_italic", "_underlined", "_strikethrough"};

    bool Brush::* TraitFieldFor(std::string_view suffix) {
        if (suffix == "_bold") {
            return &Brush::bold;
        }
        if (suffix == "_italic") {
            return &Brush::italic;
        }
        if (suffix == "_underlined") {
            return &Brush::underlined;
        }
        if (suffix == "_strikethrough") {
            return &Brush::strikethrough;
        }
        return nullptr;
    }

} // namespace

std::vector<std::string> ThemeKeys() {
    std::vector<std::string> keys;
    keys.reserve(std::size(kColorKeys) + std::size(kBrushKeys) * 6);
    for (const ThemeColorKey& entry : kColorKeys) {
        keys.emplace_back(entry.key);
    }
    for (const ThemeBrushKey& entry : kBrushKeys) {
        keys.emplace_back(std::string(entry.prefix) + "_background");
        keys.emplace_back(std::string(entry.prefix) + "_foreground");
        for (const std::string_view suffix : kTraitSuffixes) {
            keys.emplace_back(std::string(entry.prefix) + std::string(suffix));
        }
    }
    return keys;
}

std::optional<std::string> ThemeValueByKey(const Theme& theme, std::string_view key) {
    for (const ThemeColorKey& entry : kColorKeys) {
        if (entry.key == key) {
            return ColorToToken(theme.*entry.field);
        }
    }
    for (const ThemeBrushKey& entry : kBrushKeys) {
        // Suffix-checked for the same reason SetThemeColorByKey is: the
        // "scroll_bar" prefix must never claim "scroll_bar_disabled_*".
        if (!key.starts_with(entry.prefix)) {
            continue;
        }
        const std::string_view suffix = key.substr(entry.prefix.size());
        if (suffix == "_background") {
            return ColorToToken((theme.*entry.field).background);
        }
        if (suffix == "_foreground") {
            return ColorToToken((theme.*entry.field).foreground);
        }
        if (bool Brush::* traitField = TraitFieldFor(suffix); traitField != nullptr) {
            return BoolToken((theme.*entry.field).*traitField);
        }
    }
    return std::nullopt;
}

bool SetThemeColorByKey(Theme& theme, std::string_view key, std::string_view token) {
    for (const ThemeColorKey& entry : kColorKeys) {
        if (entry.key == key) {
            // A malformed token assigns nothing.
            const std::optional<Color> color = ParseColorToken(token);
            if (!color) {
                return false;
            }
            theme.*entry.field = *color;
            return true;
        }
    }
    for (const ThemeBrushKey& entry : kBrushKeys) {
        // Suffix-checked, so the "scroll_bar" prefix can never claim
        // "scroll_bar_disabled_foreground" -- the non-matching suffix
        // just lets the scan continue to the right entry.
        if (!key.starts_with(entry.prefix)) {
            continue;
        }
        const std::string_view suffix = key.substr(entry.prefix.size());
        if (suffix == "_background" || suffix == "_foreground") {
            const std::optional<Color> color = ParseColorToken(token);
            if (!color) {
                return false;
            }
            ((suffix == "_background") ? (theme.*entry.field).background : (theme.*entry.field).foreground) = *color;
            return true;
        }
        // bold/italic/underlined/strikethrough-round-trip follow-up: the
        // same "malformed token assigns nothing" contract as the color
        // suffixes above, via ParseBoolToken instead of ParseColorToken.
        bool Brush::* traitField = nullptr;
        if (suffix == "_bold") {
            traitField = &Brush::bold;
        }
        else if (suffix == "_italic") {
            traitField = &Brush::italic;
        }
        else if (suffix == "_underlined") {
            traitField = &Brush::underlined;
        }
        else if (suffix == "_strikethrough") {
            traitField = &Brush::strikethrough;
        }
        if (traitField != nullptr) {
            const std::optional<bool> value = ParseBoolToken(token);
            if (!value) {
                return false;
            }
            (theme.*entry.field).*traitField = *value;
            return true;
        }
    }
    return false; // unrecognized key
}

} // namespace ned::ui
