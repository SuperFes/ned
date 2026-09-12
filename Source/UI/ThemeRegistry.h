//
// The built-in theme name registry (rich-theme-set follow-up, Phase 1) --
// the same "resolve a name against a compile-time factory table" shape
// BundledLanguages() established for modes, applied to
// themes. This is what `ned/set-theme` names resolve against at startup
// (main.cpp) and what the select-theme picker's candidate list is built
// from (BufferView). Phase 2/3's palette-derived themes get added to the
// one table in ThemeRegistry.cpp and become reachable everywhere at once.
//
// Deliberately not a runtime-mutable registry (no RegisterTheme): every
// theme is compiled in, so a fixed table is the honest shape -- if Janet-
// defined themes ever land (ROADMAP, Phase 6's "not yet Janet-scriptable"
// note), that's the point to revisit, not before.
//

#ifndef NED_UI_THEMEREGISTRY_H
#define NED_UI_THEMEREGISTRY_H

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "UI/Theme.h"

namespace ned::ui {

// std::nullopt for an unknown name, not an error -- mirroring
// treesitter::LanguageByName/editor::ModeByName's graceful-fallback
// convention.
//
// Matching is normalized rather than exact: case is ignored and spaces and
// underscores are equivalent to hyphens, so "Gruvbox Dark", "gruvbox-dark"
// and "GRUVBOX_DARK" all resolve to the same theme. That is what lets the
// picker show and persist proper-case display names while every
// (ned/set-theme "gruvbox-dark") already written in an init.janet keeps
// working untouched.
[[nodiscard]] std::optional<Theme> ThemeByName(std::string_view name);

// Every registered *canonical* name, sorted -- the identifier form, which
// is what Theme::name carries and what a theme file round-trips.
[[nodiscard]] std::vector<std::string> ThemeNames();

// The same name as the picker shows it: "gruvbox-dark" -> "Gruvbox Dark".
// Derived mechanically from the canonical form (hyphens become spaces,
// each word's first letter is capitalized) rather than kept in a second
// table, so a theme cannot be added to the registry and forgotten here.
// A name that is not registered is title-cased and returned anyway --
// callers use this to phrase messages about names that failed to resolve.
[[nodiscard]] std::string ThemeDisplayName(std::string_view canonical);

// Every registered theme's display name, sorted by display name -- the
// select-theme picker's candidate list.
[[nodiscard]] std::vector<std::string> ThemeDisplayNames();

} // namespace ned::ui

#endif // NED_UI_THEMEREGISTRY_H
