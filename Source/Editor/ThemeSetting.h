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

namespace ned::editor {

void                      SetPreferredThemeName(const std::string& name);
[[nodiscard]] std::string PreferredThemeName();

} // namespace ned::editor

#endif // NED_EDITOR_THEMESETTING_H
