//
// Persistence for a terminal-detected Theme (see TerminalColorProbe.h) as a
// small human-readable "key=value" text file -- deliberately not Janet: this
// is a cache of previously-detected colors, not a scripting API, so it
// doesn't blur the "hardcoded C++ themes for now" line drawn for Theme
// selection in general (see ROADMAP.md's Phase 6 notes).
//

#ifndef NED_UI_THEMEFILE_H
#define NED_UI_THEMEFILE_H

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "Theme.h"

namespace ned::ui {

// Renders a Theme as "key=value" lines: hex colors ("#rrggbb") or the
// sentinel "default" for ui::Color::Default, which lets a background (or
// the echo area's) stay a pass-through rather than an opaque snapshot -- the
// only way to preserve a terminal's own transparency/blur, since no OSC
// query reliably reports an actual alpha value (Konsole's transparency, for
// instance, is a compositor/window effect, not a queryable cell property).
[[nodiscard]] std::string SerializeTheme(const Theme& theme);

// Parses SerializeTheme's format. Any key that's missing, unrecognized, or
// fails to parse keeps its value from `base` instead of erroring -- a
// partially hand-edited file degrades gracefully rather than failing
// outright.
[[nodiscard]] Theme ParseTheme(std::string_view text, const Theme& base);

// $XDG_CONFIG_HOME/ned/theme.txt, falling back to $HOME/.config/ned/theme.txt
// if XDG_CONFIG_HOME is unset or empty. Throws std::runtime_error if neither
// is usable. Mirrors Janet/InitFile.h's resolution exactly.
[[nodiscard]] std::filesystem::path ThemeFilePath();

void SaveThemeFile(const Theme& theme, const std::filesystem::path& path);

// Returns std::nullopt if the file doesn't exist; propagates std::runtime_error
// on an I/O failure (as opposed to a parse issue, which degrades gracefully
// per ParseTheme above and never throws).
[[nodiscard]] std::optional<Theme> LoadThemeFile(const std::filesystem::path& path);

} // namespace ned::ui

#endif // NED_UI_THEMEFILE_H
