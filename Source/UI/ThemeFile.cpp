#include "ThemeFile.h"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace ned::ui {

namespace {

    // The one key<->field table both SerializeTheme and ParseTheme walk
    // (theme-editing follow-up) -- replaces the old hand-mirrored pair of a
    // 50-line serializer and a 50-branch parser, which had silently drifted:
    // every per-SyntaxClass color added since the bundle-remaining-grammars
    // follow-up (function/type/constant/variable/..., 19 fields) was never
    // serialized at all, fine for --detect-theme's chrome-only output but
    // fatal for save-theme's "write it out, hand-edit it, load it back"
    // round-trip. A shared table makes that drift structurally impossible:
    // a field is either here (serialized AND parsed) or not.
    //
    // Key names are the file format -- existing keys must never be renamed
    // (older theme.txt files keep working; unrecognized keys are ignored on
    // parse for forward compatibility, see ParseTheme).
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
        // originally documented: the ANSI fallback themes made a Palette16
        // endpoint genuinely meaningful (equal endpoints, which
        // Color::Interpolate returns unchanged -- see its own comment), so
        // the old restriction would break their round-trip for no benefit.
        {"mode_line_gradient_start", &Theme::modeLineGradientStart},
        {"mode_line_gradient_end", &Theme::modeLineGradientEnd},
        {"mode_line_focused_gradient_start", &Theme::modeLineFocusedGradientStart},
        {"mode_line_focused_gradient_end", &Theme::modeLineFocusedGradientEnd},
        {"line_number_foreground", &Theme::lineNumberForeground},
        {"current_line_number_foreground", &Theme::currentLineNumberForeground},
        {"selection_background", &Theme::selectionBackground},
        {"isearch_match_background", &Theme::isearchMatchBackground},
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
        {"execution_line_background", &Theme::executionLineBackground},
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
    // pairs (bold/italic still deliberately don't round-trip -- the same
    // pre-existing limitation as always, see ThemeFile.h).
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

} // namespace

std::string SerializeTheme(const Theme& theme) {
    std::ostringstream out;
    for (const ThemeColorKey& entry : kColorKeys) {
        out << entry.key << '=' << ColorToToken(theme.*entry.field) << '\n';
    }
    for (const ThemeBrushKey& entry : kBrushKeys) {
        out << entry.prefix << "_background=" << ColorToToken((theme.*entry.field).background) << '\n';
        out << entry.prefix << "_foreground=" << ColorToToken((theme.*entry.field).foreground) << '\n';
    }
    return out.str();
}

bool SetThemeColorByKey(Theme& theme, std::string_view key, std::string_view token) {
    // A malformed token assigns nothing, same as ParseTheme always did.
    const std::optional<Color> color = ParseColorToken(token);
    if (!color) {
        return false;
    }

    for (const ThemeColorKey& entry : kColorKeys) {
        if (entry.key == key) {
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
        if (suffix == "_background") {
            (theme.*entry.field).background = *color;
            return true;
        }
        if (suffix == "_foreground") {
            (theme.*entry.field).foreground = *color;
            return true;
        }
    }
    return false; // unrecognized key
}

Theme ParseTheme(std::string_view text, const Theme& base) {
    Theme result = base;

    std::istringstream in{std::string(text)};
    std::string        line;
    while (std::getline(in, line)) {
        const auto eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        // A false return (malformed value, unrecognized key) keeps base's
        // own value -- ignored deliberately, forward-compatible with files
        // written by newer versions.
        SetThemeColorByKey(result, std::string_view(line).substr(0, eq), std::string_view(line).substr(eq + 1));
    }

    return result;
}

std::string SerializeThemeJanet(const Theme& theme) {
    std::ostringstream out;
    out << "# Generated by ned's save-theme command -- a snapshot of the theme that\n"
           "# was active when it ran, as plain Janet. Edit freely; load it from\n"
           "# init.janet with (dofile \"<this file's path>\") to make it the\n"
           "# startup theme. Every color is set explicitly, so the base theme\n"
           "# underneath doesn't show through anywhere.\n";
    for (const ThemeColorKey& entry : kColorKeys) {
        out << "(ned/theme-set \"" << entry.key << "\" \"" << ColorToToken(theme.*entry.field) << "\")\n";
    }
    for (const ThemeBrushKey& entry : kBrushKeys) {
        out << "(ned/theme-set \"" << entry.prefix << "_background\" \""
            << ColorToToken((theme.*entry.field).background) << "\")\n";
        out << "(ned/theme-set \"" << entry.prefix << "_foreground\" \""
            << ColorToToken((theme.*entry.field).foreground) << "\")\n";
    }
    return out.str();
}

std::filesystem::path ThemeFilePath() {
    if (const char* xdgConfigHome = std::getenv("XDG_CONFIG_HOME"); xdgConfigHome && *xdgConfigHome) {
        return std::filesystem::path(xdgConfigHome) / "ned" / "theme.txt";
    }

    if (const char* home = std::getenv("HOME"); home && *home) {
        return std::filesystem::path(home) / ".config" / "ned" / "theme.txt";
    }

    throw std::runtime_error("ned: cannot determine config directory (neither XDG_CONFIG_HOME nor HOME is set)");
}

std::filesystem::path ThemeJanetFilePath() {
    return ThemeFilePath().parent_path() / "theme.janet";
}

namespace {

    void WriteThemeContent(const std::string& content, const std::filesystem::path& path) {
        std::filesystem::create_directories(path.parent_path());

        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file) {
            throw std::runtime_error("ned: cannot open theme file for writing: " + path.string());
        }

        file.write(content.data(), static_cast<std::streamsize>(content.size()));
        if (!file) {
            throw std::runtime_error("ned: error writing theme file: " + path.string());
        }
    }

} // namespace

void SaveThemeFile(const Theme& theme, const std::filesystem::path& path) {
    WriteThemeContent(SerializeTheme(theme), path);
}

void SaveThemeJanetFile(const Theme& theme, const std::filesystem::path& path) {
    WriteThemeContent(SerializeThemeJanet(theme), path);
}

std::optional<Theme> LoadThemeFile(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        return std::nullopt;
    }

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("ned: cannot open theme file for reading: " + path.string());
    }

    std::ostringstream content;
    content << file.rdbuf();

    return ParseTheme(content.str(), DarkTheme());
}

} // namespace ned::ui
