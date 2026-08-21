//
// Theme persistence, in two formats sharing one key table (ThemeFile.cpp's
// kColorKeys/kBrushKeys):
//
// - "key=value" text (SerializeTheme/ParseTheme, theme.txt) -- the
//   --detect-theme cache. Deliberately not Janet: a cache of previously-
//   detected colors, not a scripting API (the original Phase 6 reasoning,
//   still true for this format).
// - Runnable Janet (SerializeThemeJanet, theme.janet) -- the save-theme
//   command's output, theme-editing follow-up: one (ned/theme-set ...) call
//   per color, hand-editable, loaded from init.janet via (dofile ...). This
//   IS the scripting API the .txt format deliberately isn't -- the
//   accumulated calls land in Editor/ThemeSetting.h's override store and
//   main.cpp applies them via SetThemeColorByKey below.
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

// Theme-editing follow-up: the Janet-facing side of the same key table
// SerializeTheme/ParseTheme walk (see ThemeFile.cpp's kColorKeys comment).
//
// SetThemeColorByKey assigns one keyed color -- exactly one ParseTheme line's
// worth -- returning false for an unrecognized key or unparseable token
// (caller decides whether that's worth reporting; ParseTheme itself stays
// silently forward-compatible). This is what main.cpp uses to apply
// `ned/theme-set` overrides from init.janet on top of the selected theme.
bool SetThemeColorByKey(Theme& theme, std::string_view key, std::string_view token);

// Renders a Theme as a runnable Janet script -- one
// `(ned/theme-set "<key>" "<color>")` call per keyed color, same keys and
// tokens as SerializeTheme -- for the save-theme command: written out,
// hand-edited, then loaded from init.janet via a plain (dofile ...). Same
// bold/italic limitation as the key=value format (colors only).
[[nodiscard]] std::string SerializeThemeJanet(const Theme& theme);

// $XDG_CONFIG_HOME/ned/theme.janet -- the save-theme command's output path,
// same XDG resolution as ThemeFilePath above.
[[nodiscard]] std::filesystem::path ThemeJanetFilePath();

// SerializeThemeJanet to disk -- SaveThemeFile's exact write/error contract,
// different serialization.
void SaveThemeJanetFile(const Theme& theme, const std::filesystem::path& path);

} // namespace ned::ui

#endif // NED_UI_THEMEFILE_H
