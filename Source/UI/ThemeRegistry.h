//
// The built-in theme name registry (rich-theme-set follow-up, Phase 1) --
// the same "resolve a name against a compile-time factory table" shape
// ModeOverrides' BundledModeFactories established for modes, applied to
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
// convention. Names are exact matches ("dark", "ansi-light", ...).
[[nodiscard]] std::optional<Theme> ThemeByName(std::string_view name);

// Every registered name, sorted -- the select-theme picker's candidate
// list, and what an "unknown theme" error can suggest from.
[[nodiscard]] std::vector<std::string> ThemeNames();

} // namespace ned::ui

#endif // NED_UI_THEMEREGISTRY_H
