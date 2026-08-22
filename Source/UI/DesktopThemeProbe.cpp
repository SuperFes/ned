#include "DesktopThemeProbe.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>

#include "Editor/Process/ChildProcess.h"

namespace ned::ui {

namespace {

    using ned::editor::process::ChildProcess;
    using ned::editor::process::ResolveExecutable;

    // Runs argv to completion and returns its stdout iff it exits 0 --
    // nullopt on any failure (not found, spawn error, non-zero exit),
    // mirroring ProjectSearch.cpp's own RunCapturingStdout contract, just
    // built on the shared ChildProcess primitive instead of a second
    // hand-rolled posix_spawn (this file has no rg-style "must avoid one
    // extra fork" performance constraint -- a probe that runs once at
    // startup, not per keystroke).
    std::optional<std::string> RunCapturingStdout(const std::vector<std::string>& argv) {
        if (!ResolveExecutable(argv.front())) {
            return std::nullopt;
        }
        try {
            ChildProcess child(argv);
            std::string  output;
            std::string  chunk;
            while (!(chunk = child.ReadSome()).empty()) {
                output += chunk;
            }
            const std::optional<int> exitCode = child.WaitForExit();
            if (exitCode && *exitCode == 0) {
                return output;
            }
        }
        catch (const std::runtime_error&) {
            // Not found, pipe/spawn failure -- treated identically to "not installed".
        }
        return std::nullopt;
    }

    std::uint8_t FloatToByte(double f) {
        return static_cast<std::uint8_t>(std::lround(std::clamp(f, 0.0, 1.0) * 255.0));
    }

    std::string Lowercase(std::string_view s) {
        std::string result(s);
        std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return std::tolower(c); });
        return result;
    }

    // Strips gsettings' own single-quoting ('prefer-dark' -> prefer-dark),
    // if present -- gsettings quotes string-typed values but not others.
    std::string_view Unquote(std::string_view s) {
        while (!s.empty() && (s.front() == ' ' || s.front() == '\n' || s.front() == '\t')) {
            s.remove_prefix(1);
        }
        while (!s.empty() && (s.back() == ' ' || s.back() == '\n' || s.back() == '\t')) {
            s.remove_suffix(1);
        }
        if (s.size() >= 2 && s.front() == '\'' && s.back() == '\'') {
            return s.substr(1, s.size() - 2);
        }
        return s;
    }

    // Minimal line-based INI reader -- just enough for kdeglobals'
    // flat [Section]/key=value shape, no [Section][SubGroup] nesting or
    // $[...] KIOSK indirection (neither appears in a plain user
    // kdeglobals).
    std::optional<std::string> IniValue(std::string_view content, std::string_view section, std::string_view key) {
        std::istringstream in{std::string(content)};
        std::string        line;
        std::string        currentSection;
        while (std::getline(in, line)) {
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
                line.pop_back();
            }
            if (line.empty()) {
                continue;
            }
            if (line.front() == '[' && line.back() == ']') {
                currentSection = line.substr(1, line.size() - 2);
                continue;
            }
            if (currentSection != section) {
                continue;
            }
            const std::size_t eq = line.find('=');
            if (eq == std::string::npos) {
                continue;
            }
            if (std::string_view(line).substr(0, eq) == key) {
                return line.substr(eq + 1);
            }
        }
        return std::nullopt;
    }

    // "r,g,b" (KDE's own on-disk color format, plain decimal, no spaces
    // required around the commas) -> Color. nullopt on anything else.
    std::optional<Color> ParseKdeRgbTriplet(std::string_view value) {
        int                    channel[3] = {0, 0, 0};
        std::size_t            start      = 0;
        for (int i = 0; i < 3; ++i) {
            const std::size_t comma = (i < 2) ? value.find(',', start) : value.size();
            if (comma == std::string_view::npos) {
                return std::nullopt;
            }
            const std::string_view field = value.substr(start, comma - start);
            const auto [ptr, ec]         = std::from_chars(field.data(), field.data() + field.size(), channel[i]);
            if (ec != std::errc() || ptr != field.data() + field.size()) {
                return std::nullopt;
            }
            start = comma + 1;
        }
        for (int c : channel) {
            if (c < 0 || c > 255) {
                return std::nullopt;
            }
        }
        return Color::RGB(static_cast<std::uint8_t>(channel[0]), static_cast<std::uint8_t>(channel[1]),
                           static_cast<std::uint8_t>(channel[2]));
    }

    // GNOME/Adwaita's fixed accent palette (libadwaita's AdwAccentColor
    // enum) -- the only nine names `gsettings get
    // org.gnome.desktop.interface accent-color` can ever return.
    std::optional<Color> GnomeAccentTable(std::string_view name) {
        static const std::pair<std::string_view, Color> kTable[] = {
            {"blue", Color::RGB(0x35, 0x84, 0xe4)},   {"teal", Color::RGB(0x21, 0x90, 0xa4)},
            {"green", Color::RGB(0x3a, 0x94, 0x4a)},  {"yellow", Color::RGB(0xc8, 0x88, 0x00)},
            {"orange", Color::RGB(0xed, 0x5b, 0x00)}, {"red", Color::RGB(0xe6, 0x2d, 0x42)},
            {"pink", Color::RGB(0xd5, 0x61, 0x99)},   {"purple", Color::RGB(0x91, 0x41, 0xac)},
            {"slate", Color::RGB(0x6f, 0x83, 0x96)},
        };
        for (const auto& [candidate, color] : kTable) {
            if (candidate == name) {
                return color;
            }
        }
        return std::nullopt;
    }

    std::string_view DesktopFromEnv() {
        if (const char* v = std::getenv("XDG_CURRENT_DESKTOP"); v && *v) {
            return v;
        }
        if (const char* v = std::getenv("XDG_SESSION_DESKTOP"); v && *v) {
            return v;
        }
        return {};
    }

    std::optional<std::filesystem::path> KdeGlobalsPath() {
        std::filesystem::path base;
        if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg) {
            base = xdg;
        }
        else if (const char* home = std::getenv("HOME"); home && *home) {
            base = std::filesystem::path(home) / ".config";
        }
        else {
            return std::nullopt;
        }
        const std::filesystem::path path = base / "kdeglobals";
        std::error_code             ec;
        return std::filesystem::exists(path, ec) ? std::optional(path) : std::nullopt;
    }

    // Both portal replies (Read's own JSON-RPC-free-form output, wrapping
    // the requested value in a GVariant) come back through gdbus/busctl as
    // one line of text -- calling the same portal method twice, once per
    // key, keeps this a single value per call rather than needing to parse
    // ReadAll's whole-namespace dict.
    std::optional<std::string> QueryPortalSetting(const std::string& tool, std::string_view namespaceName, std::string_view key) {
        std::vector<std::string> argv;
        if (tool == "gdbus") {
            argv = {"gdbus",
                    "call",
                    "--session",
                    "--dest",
                    "org.freedesktop.portal.Desktop",
                    "--object-path",
                    "/org/freedesktop/portal/desktop",
                    "--method",
                    "org.freedesktop.portal.Settings.Read",
                    std::string(namespaceName),
                    std::string(key)};
        }
        else {
            argv = {"busctl",       "--user", "call", "org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
                     "org.freedesktop.portal.Settings", "Read", "ss", std::string(namespaceName), std::string(key)};
        }
        return RunCapturingStdout(argv);
    }

} // namespace

std::optional<bool> ParsePortalColorScheme(std::string_view reply) {
    // gdbus: "<uint32 1>"; busctl: "u 1" -- both reduce to one
    // unambiguously-labeled integer token.
    static const std::regex kPattern(R"((?:uint32|\bu\b)\s+(\d+))");
    std::cmatch              match;
    if (!std::regex_search(reply.data(), reply.data() + reply.size(), match, kPattern)) {
        return std::nullopt;
    }
    const int value = std::atoi(match[1].str().c_str());
    if (value == 1) {
        return true;
    }
    if (value == 2) {
        return false;
    }
    return std::nullopt; // 0 = no preference, or an unrecognized value
}

std::optional<Color> ParsePortalAccentColor(std::string_view reply) {
    // gdbus: "(0.2, 0.4, 0.8)"; busctl: "(ddd) 0.2 0.4 0.8" -- both reduce
    // to three plain floats, comma- or space-separated, in RGB order.
    static const std::regex kPattern(
        R"(([0-9]*\.[0-9]+|[0-9]+)[,\s]+([0-9]*\.[0-9]+|[0-9]+)[,\s]+([0-9]*\.[0-9]+|[0-9]+))");
    std::cmatch match;
    if (!std::regex_search(reply.data(), reply.data() + reply.size(), match, kPattern)) {
        return std::nullopt;
    }
    const double r = std::atof(match[1].str().c_str());
    const double g = std::atof(match[2].str().c_str());
    const double b = std::atof(match[3].str().c_str());
    return Color::RGB(FloatToByte(r), FloatToByte(g), FloatToByte(b));
}

std::optional<bool> ParseGsettingsColorScheme(std::string_view reply) {
    const std::string value(Unquote(reply));
    if (value == "prefer-dark") {
        return true;
    }
    if (value == "prefer-light") {
        return false;
    }
    return std::nullopt; // "default", or an unrecognized/empty reply
}

std::optional<Color> GnomeAccentColorFromName(std::string_view reply) {
    return GnomeAccentTable(Lowercase(Unquote(reply)));
}

KdeGlobalsInfo ParseKdeGlobals(std::string_view content) {
    KdeGlobalsInfo info;

    if (const auto scheme = IniValue(content, "General", "ColorScheme")) {
        const std::string lower = Lowercase(*scheme);
        if (lower.find("dark") != std::string::npos) {
            info.preferDark = true;
        }
        else if (lower.find("light") != std::string::npos) {
            info.preferDark = false;
        }
    }

    if (const auto accent = IniValue(content, "General", "AccentColor")) {
        info.accent = ParseKdeRgbTriplet(*accent);
    }
    if (!info.accent) {
        if (const auto selection = IniValue(content, "Colors:Selection", "BackgroundNormal")) {
            info.accent = ParseKdeRgbTriplet(*selection);
        }
    }

    return info;
}

std::optional<DesktopThemeInfo> ProbeDesktopTheme() {
    std::optional<bool>  preferDark;
    std::optional<Color> accent;

    // Step 1: the freedesktop portal, whichever D-Bus CLI is available --
    // deliberately DE-agnostic, tried first regardless of
    // $XDG_CURRENT_DESKTOP.
    for (const char* tool : {"gdbus", "busctl"}) {
        if (preferDark && accent) {
            break;
        }
        if (!ResolveExecutable(tool)) {
            continue;
        }
        if (!preferDark) {
            if (const auto reply = QueryPortalSetting(tool, "org.freedesktop.appearance", "color-scheme")) {
                preferDark = ParsePortalColorScheme(*reply);
            }
        }
        if (!accent) {
            if (const auto reply = QueryPortalSetting(tool, "org.freedesktop.appearance", "accent-color")) {
                accent = ParsePortalAccentColor(*reply);
            }
        }
    }

    const std::string desktop = Lowercase(DesktopFromEnv());

    // Step 2: GNOME-specific fallback.
    if ((!preferDark || !accent) && desktop.find("gnome") != std::string::npos && ResolveExecutable("gsettings")) {
        if (!preferDark) {
            if (const auto reply = RunCapturingStdout({"gsettings", "get", "org.gnome.desktop.interface", "color-scheme"})) {
                preferDark = ParseGsettingsColorScheme(*reply);
            }
        }
        if (!accent) {
            if (const auto reply = RunCapturingStdout({"gsettings", "get", "org.gnome.desktop.interface", "accent-color"})) {
                accent = GnomeAccentColorFromName(*reply);
            }
        }
    }

    // Step 3: KDE/Plasma-specific fallback.
    if ((!preferDark || !accent) && (desktop.find("kde") != std::string::npos || desktop.find("plasma") != std::string::npos)) {
        if (const auto path = KdeGlobalsPath()) {
            std::ifstream file(*path);
            if (file) {
                std::ostringstream buffer;
                buffer << file.rdbuf();
                const KdeGlobalsInfo info = ParseKdeGlobals(buffer.str());
                if (!preferDark) {
                    preferDark = info.preferDark;
                }
                if (!accent) {
                    accent = info.accent;
                }
            }
        }
    }

    if (!preferDark && !accent) {
        return std::nullopt;
    }

    DesktopThemeInfo result;
    if (preferDark) {
        result.preferDark = *preferDark;
    }
    result.accent = accent;
    return result;
}

Theme BuildDesktopTheme(const DesktopThemeInfo& info) {
    Theme result = info.preferDark ? DarkTheme() : LightTheme();

    if (info.accent) {
        // Same accent-application block TerminalColorProbe::BuildDetectedTheme
        // uses for its own single detected accent color (palette[5]) --
        // see that function's own comment for why these particular fields.
        result.borderAccent.foreground = *info.accent;
        result.keywordForeground       = *info.accent;
        result.modeLineFocusedGradientStart =
            Color::Interpolate(0.6F, result.modeLineGradientStart, *info.accent);
        result.modeLineFocusedGradientEnd = Color::Interpolate(0.6F, result.modeLineGradientEnd, *info.accent);
    }

    return result;
}

} // namespace ned::ui
