//
// The user's preferred startup theme name (rich-theme-set follow-up,
// Phase 1). One process-wide string, mutex-guarded static state --
// TabWidth.h/FormatOnSave.h's exact pattern. Configured from Janet via
// ned/set-theme (e.g. (ned/set-theme "ansi-dark") in init.janet); empty
// means "no preference," the default, and an empty Set clears it back to
// that (FormatOnSave's own empty-string convention).
//
// Deliberately just a *string* here in Source/Editor/: resolving it
// against the actual theme table is ui::ThemeByName (UI/ThemeRegistry.h),
// done by main.cpp at startup -- keeping this layer free of any UI
// dependency, the same split SyntaxTheme.h already makes for per-class
// style overrides. An unresolvable name is main.cpp's to report, not an
// error here: the registry the name resolves against doesn't exist at
// init.janet-load time from this layer's point of view.
//

#ifndef NED_EDITOR_THEMESETTING_H
#define NED_EDITOR_THEMESETTING_H

#include <string>
#include <utility>
#include <vector>

namespace ned::editor {

void                      SetPreferredThemeName(const std::string& name);
[[nodiscard]] std::string PreferredThemeName();

// Theme-editing follow-up: per-field color overrides accumulated from Janet
// (ned/theme-set, typically a whole (dofile ".../theme.janet") worth --
// save-theme's output). Same string-only layering as the name above: keys
// are ui::ThemeFile's own serialization keys, but *this* layer never
// interprets them -- main.cpp applies them over the selected theme via
// ui::SetThemeColorByKey after init.janet has loaded. Kept in insertion
// order, applied in insertion order, so a later (ned/theme-set ...) for the
// same key wins, matching Janet's own sequential evaluation.
void                                                           AddThemeColorOverride(const std::string& key, const std::string& token);
[[nodiscard]] std::vector<std::pair<std::string, std::string>> ThemeColorOverrides();
// Process-wide state needs a reset seam for tests (the SyntaxThemeGuard
// precedent); not reachable from Janet.
void ClearThemeColorOverrides();

} // namespace ned::editor

#endif // NED_EDITOR_THEMESETTING_H
