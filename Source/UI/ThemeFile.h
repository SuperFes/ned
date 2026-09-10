//
// The theme key vocabulary: the one table mapping a settable name
// ("keyword_foreground", "active_tab_bold") to the ui::Theme field it names,
// plus the read/write pair over it.
//
// A theme is written as `(ned/theme-set "key" "value")` calls in the user's
// own init.janet, on top of whichever bundled theme ned/set-theme picks --
// so this table *is* the format, and Docs/Themes.md's key listing is its
// documentation (ThemeKeyDocsTest holds the two against each other). Key
// names must never be renamed: an existing init.janet keeps working, and an
// unrecognized key is reported once at startup rather than being an error.
//
// Two things used to live here and no longer do: a plain `key=value` file
// (the `ned --detect-theme` cache) and a whole-theme Janet serializer behind
// `M-x save-theme`. Detection is UI/DesktopThemeProbe.h's job, and a
// theme small enough to be worth writing is small enough to write by hand --
// a handful of overrides on a bundled base, not a 70-field snapshot.
//

#ifndef NED_UI_THEMEFILE_H
#define NED_UI_THEMEFILE_H

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Theme.h"

namespace ned::ui {

// SetThemeColorByKey assigns one keyed color or Brush trait (bold/italic/
// underlined/strikethrough, "true"/"false" tokens) -- exactly one serialized
// line's worth -- returning false for an unrecognized key or unparseable
// token. This is what UI/ThemeResolve.h uses to apply `ned/theme-set`
// overrides from init.janet on top of the selected theme.
bool SetThemeColorByKey(Theme& theme, std::string_view key, std::string_view token);

// Every key SetThemeColorByKey accepts, in table order: the colour keys
// first, then each Brush's <prefix>_background/_foreground pair followed by
// its four trait flags.
//
// This is the vocabulary a theme is written in, so it is the thing
// Docs/Themes.md has to list in full -- ThemeKeyDocsTest holds the two
// against each other, which is what keeps a newly added Theme field from
// being settable but undocumented. It is also how the theme tests walk every
// colour of every bundled theme without naming ~70 fields by hand.
[[nodiscard]] std::vector<std::string> ThemeKeys();

// The value SetThemeColorByKey would round-trip for `key`, as the same token
// form it accepts ("#rrggbb", "#rrggbbaa", "default", "true"/"false"), or
// std::nullopt for an unrecognized key. The read half of the pair -- there
// was no need for one while a whole-theme serializer existed.
[[nodiscard]] std::optional<std::string> ThemeValueByKey(const Theme& theme, std::string_view key);

} // namespace ned::ui

#endif // NED_UI_THEMEFILE_H
