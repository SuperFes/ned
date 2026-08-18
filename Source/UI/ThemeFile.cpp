#include "ThemeFile.h"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace ned::ui {

namespace {

    // mode_line_gradient_start/end only ever accept the hex form -- a
    // gradient endpoint can't meaningfully be "default" or a palette index,
    // even though Theme.h's own field type (Color) doesn't restrict that at
    // the type level (see that field's own comment for why). ColorToToken/
    // ParseColorToken (used everywhere else in this file) now live in
    // Theme.h/.cpp -- Janet-configurable-syntax-theme follow-up, needed by
    // Theme::BrushFor()'s override merge too, not just this file's own
    // save/load.
    std::optional<Color> ParseTrueColorToken(std::string_view token) {
        const auto color = ParseColorToken(token);
        return (color && color->kind == Color::Kind::TrueColor) ? color : std::nullopt;
    }

} // namespace

std::string SerializeTheme(const Theme& theme) {
    std::ostringstream out;
    out << "background=" << ColorToToken(theme.background) << '\n';
    out << "default_foreground=" << ColorToToken(theme.defaultForeground) << '\n';
    out << "comment_foreground=" << ColorToToken(theme.commentForeground) << '\n';
    out << "string_foreground=" << ColorToToken(theme.stringForeground) << '\n';
    out << "keyword_foreground=" << ColorToToken(theme.keywordForeground) << '\n';
    out << "number_foreground=" << ColorToToken(theme.numberForeground) << '\n';
    out << "mode_line_foreground=" << ColorToToken(theme.modeLineForeground) << '\n';
    out << "mode_line_gradient_start=" << ColorToToken(theme.modeLineGradientStart) << '\n';
    out << "mode_line_gradient_end=" << ColorToToken(theme.modeLineGradientEnd) << '\n';
    out << "echo_area_background=" << ColorToToken(theme.echoArea.background) << '\n';
    out << "echo_area_foreground=" << ColorToToken(theme.echoArea.foreground) << '\n';
    out << "line_number_foreground=" << ColorToToken(theme.lineNumberForeground) << '\n';
    out << "current_line_number_foreground=" << ColorToToken(theme.currentLineNumberForeground) << '\n';
    out << "selection_background=" << ColorToToken(theme.selectionBackground) << '\n';
    out << "isearch_match_background=" << ColorToToken(theme.isearchMatchBackground) << '\n';
    out << "tab_bar_background=" << ColorToToken(theme.tabBar.background) << '\n';
    out << "tab_bar_foreground=" << ColorToToken(theme.tabBar.foreground) << '\n';
    out << "active_tab_background=" << ColorToToken(theme.activeTab.background) << '\n';
    out << "active_tab_foreground=" << ColorToToken(theme.activeTab.foreground) << '\n';
    out << "scroll_bar_background=" << ColorToToken(theme.scrollBar.background) << '\n';
    out << "scroll_bar_foreground=" << ColorToToken(theme.scrollBar.foreground) << '\n';
    out << "scroll_bar_disabled_background=" << ColorToToken(theme.scrollBarDisabled.background) << '\n';
    out << "scroll_bar_disabled_foreground=" << ColorToToken(theme.scrollBarDisabled.foreground) << '\n';
    out << "binary_foreground=" << ColorToToken(theme.binaryForeground) << '\n';
    out << "link_foreground=" << ColorToToken(theme.linkForeground) << '\n';
    out << "unsaved_change_indicator=" << ColorToToken(theme.unsavedChangeIndicator) << '\n';
    out << "headline_level1_foreground=" << ColorToToken(theme.headlineLevel1Foreground) << '\n';
    out << "headline_level2_foreground=" << ColorToToken(theme.headlineLevel2Foreground) << '\n';
    out << "headline_level3_foreground=" << ColorToToken(theme.headlineLevel3Foreground) << '\n';
    out << "todo_keyword_foreground=" << ColorToToken(theme.todoKeywordForeground) << '\n';
    out << "done_keyword_foreground=" << ColorToToken(theme.doneKeywordForeground) << '\n';
    out << "checkbox_foreground=" << ColorToToken(theme.checkboxForeground) << '\n';
    out << "underline_foreground=" << ColorToToken(theme.underlineForeground) << '\n';
    out << "strikethrough_foreground=" << ColorToToken(theme.strikethroughForeground) << '\n';
    out << "keyword_modifier_foreground=" << ColorToToken(theme.keywordModifierForeground) << '\n';
    out << "method_foreground=" << ColorToToken(theme.methodForeground) << '\n';
    out << "constructor_foreground=" << ColorToToken(theme.constructorForeground) << '\n';
    out << "label_foreground=" << ColorToToken(theme.labelForeground) << '\n';
    out << "return_type_foreground=" << ColorToToken(theme.returnTypeForeground) << '\n';
    out << "include_path_foreground=" << ColorToToken(theme.includePathForeground) << '\n';
    return out.str();
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
        const std::string_view key   = std::string_view(line).substr(0, eq);
        const std::string_view value = std::string_view(line).substr(eq + 1);

        if (key == "background") {
            if (const auto c = ParseColorToken(value))
                result.background = *c;
        }
        else if (key == "default_foreground") {
            if (const auto c = ParseColorToken(value))
                result.defaultForeground = *c;
        }
        else if (key == "comment_foreground") {
            if (const auto c = ParseColorToken(value))
                result.commentForeground = *c;
        }
        else if (key == "string_foreground") {
            if (const auto c = ParseColorToken(value))
                result.stringForeground = *c;
        }
        else if (key == "keyword_foreground") {
            if (const auto c = ParseColorToken(value))
                result.keywordForeground = *c;
        }
        else if (key == "number_foreground") {
            if (const auto c = ParseColorToken(value))
                result.numberForeground = *c;
        }
        else if (key == "mode_line_foreground") {
            if (const auto c = ParseColorToken(value))
                result.modeLineForeground = *c;
        }
        else if (key == "mode_line_gradient_start") {
            if (const auto c = ParseTrueColorToken(value))
                result.modeLineGradientStart = *c;
        }
        else if (key == "mode_line_gradient_end") {
            if (const auto c = ParseTrueColorToken(value))
                result.modeLineGradientEnd = *c;
        }
        else if (key == "echo_area_background") {
            if (const auto c = ParseColorToken(value))
                result.echoArea.background = *c;
        }
        else if (key == "echo_area_foreground") {
            if (const auto c = ParseColorToken(value))
                result.echoArea.foreground = *c;
        }
        else if (key == "line_number_foreground") {
            if (const auto c = ParseColorToken(value))
                result.lineNumberForeground = *c;
        }
        else if (key == "current_line_number_foreground") {
            if (const auto c = ParseColorToken(value))
                result.currentLineNumberForeground = *c;
        }
        else if (key == "selection_background") {
            if (const auto c = ParseColorToken(value))
                result.selectionBackground = *c;
        }
        else if (key == "isearch_match_background") {
            if (const auto c = ParseColorToken(value))
                result.isearchMatchBackground = *c;
        }
        else if (key == "tab_bar_background") {
            if (const auto c = ParseColorToken(value))
                result.tabBar.background = *c;
        }
        else if (key == "tab_bar_foreground") {
            if (const auto c = ParseColorToken(value))
                result.tabBar.foreground = *c;
        }
        else if (key == "active_tab_background") {
            if (const auto c = ParseColorToken(value))
                result.activeTab.background = *c;
        }
        else if (key == "active_tab_foreground") {
            if (const auto c = ParseColorToken(value))
                result.activeTab.foreground = *c;
        }
        else if (key == "scroll_bar_background") {
            if (const auto c = ParseColorToken(value))
                result.scrollBar.background = *c;
        }
        else if (key == "scroll_bar_foreground") {
            if (const auto c = ParseColorToken(value))
                result.scrollBar.foreground = *c;
        }
        else if (key == "scroll_bar_disabled_background") {
            if (const auto c = ParseColorToken(value))
                result.scrollBarDisabled.background = *c;
        }
        else if (key == "scroll_bar_disabled_foreground") {
            if (const auto c = ParseColorToken(value))
                result.scrollBarDisabled.foreground = *c;
        }
        else if (key == "binary_foreground") {
            if (const auto c = ParseColorToken(value))
                result.binaryForeground = *c;
        }
        else if (key == "link_foreground") {
            if (const auto c = ParseColorToken(value))
                result.linkForeground = *c;
        }
        else if (key == "unsaved_change_indicator") {
            if (const auto c = ParseColorToken(value))
                result.unsavedChangeIndicator = *c;
        }
        else if (key == "headline_level1_foreground") {
            if (const auto c = ParseColorToken(value))
                result.headlineLevel1Foreground = *c;
        }
        else if (key == "headline_level2_foreground") {
            if (const auto c = ParseColorToken(value))
                result.headlineLevel2Foreground = *c;
        }
        else if (key == "headline_level3_foreground") {
            if (const auto c = ParseColorToken(value))
                result.headlineLevel3Foreground = *c;
        }
        else if (key == "todo_keyword_foreground") {
            if (const auto c = ParseColorToken(value))
                result.todoKeywordForeground = *c;
        }
        else if (key == "done_keyword_foreground") {
            if (const auto c = ParseColorToken(value))
                result.doneKeywordForeground = *c;
        }
        else if (key == "checkbox_foreground") {
            if (const auto c = ParseColorToken(value))
                result.checkboxForeground = *c;
        }
        else if (key == "underline_foreground") {
            if (const auto c = ParseColorToken(value))
                result.underlineForeground = *c;
        }
        else if (key == "strikethrough_foreground") {
            if (const auto c = ParseColorToken(value))
                result.strikethroughForeground = *c;
        }
        else if (key == "keyword_modifier_foreground") {
            if (const auto c = ParseColorToken(value))
                result.keywordModifierForeground = *c;
        }
        else if (key == "method_foreground") {
            if (const auto c = ParseColorToken(value))
                result.methodForeground = *c;
        }
        else if (key == "constructor_foreground") {
            if (const auto c = ParseColorToken(value))
                result.constructorForeground = *c;
        }
        else if (key == "label_foreground") {
            if (const auto c = ParseColorToken(value))
                result.labelForeground = *c;
        }
        else if (key == "return_type_foreground") {
            if (const auto c = ParseColorToken(value))
                result.returnTypeForeground = *c;
        }
        else if (key == "include_path_foreground") {
            if (const auto c = ParseColorToken(value))
                result.includePathForeground = *c;
        }
        // Unrecognized keys are ignored -- forward-compatible with older files.
    }

    return result;
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

void SaveThemeFile(const Theme& theme, const std::filesystem::path& path) {
    std::filesystem::create_directories(path.parent_path());

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        throw std::runtime_error("ned: cannot open theme file for writing: " + path.string());
    }

    const std::string content = SerializeTheme(theme);
    file.write(content.data(), static_cast<std::streamsize>(content.size()));
    if (!file) {
        throw std::runtime_error("ned: error writing theme file: " + path.string());
    }
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
