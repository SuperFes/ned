#include "ThemeFile.h"

#include <array>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace ned::ui {

namespace {

    char HexDigit(int nibble) {
        return static_cast<char>(nibble < 10 ? '0' + nibble : 'a' + (nibble - 10));
    }

    std::string TrueColorToHex(const Color& c) {
        std::string                       out = "#000000";
        const std::array<std::uint8_t, 3> channels{c.red, c.green, c.blue};
        for (std::size_t i = 0; i < channels.size(); ++i) {
            out[1 + i * 2]     = HexDigit(channels[i] >> 4);
            out[1 + i * 2 + 1] = HexDigit(channels[i] & 0x0F);
        }
        return out;
    }

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

    // mode_line_gradient_start/end only ever accept the hex form -- a
    // gradient endpoint can't meaningfully be "default" or a palette index,
    // even though Theme.h's own field type (Color) doesn't restrict that at
    // the type level (see that field's own comment for why).
    std::optional<Color> ParseTrueColorToken(std::string_view token) {
        return ParseHexColor(token);
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
    out << "mode_line_gradient_start=" << TrueColorToHex(theme.modeLineGradientStart) << '\n';
    out << "mode_line_gradient_end=" << TrueColorToHex(theme.modeLineGradientEnd) << '\n';
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
