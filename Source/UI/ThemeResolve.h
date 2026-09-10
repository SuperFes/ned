//
// The theme pipeline, as one function instead of a block of main.cpp.
//
// Resolving "what theme is actually showing" is four steps -- pick a base,
// remember the machine's own accent, apply the accumulated (ned/theme-set
// ...) colour overrides, then re-parse every (ned/theme-gradient ...) /
// (ned/theme-surface ...) spec against the theme those first three produced.
// That ordering is load-bearing: a paint spec says "$bg" and only means
// something once a base theme exists to resolve it against.
//
// It lived inline in main.cpp while startup was the only caller. It is here
// because it now has three (Docs/Translucency.md phase 4b):
//
//   startup            the original caller, ResolveConfiguredTheme
//   the theme picker   swapping the base theme invalidates every paint that
//                      referenced a slot of the old one, so the applier
//                      re-runs ApplyPaintOverrides alone
//
// That last one is why ApplyPaintOverrides is public rather than a private
// tail of ResolveConfiguredTheme: select-theme replaces the base without
// touching the spec store, and a "$bg" gradient resolved against the theme
// you just switched away from is the bug this makes impossible.
//
// Lives in Source/UI/ rather than Source/Editor/ because it produces a
// ui::Theme and calls ui::ParsePaint. It reads Editor/ThemeSetting.h's and
// Editor/Variables.h's stores, which is the ordinary UI-depends-on-Editor
// direction.
//

#ifndef NED_UI_THEMERESOLVE_H
#define NED_UI_THEMERESOLVE_H

#include <optional>
#include <string>
#include <string_view>

#include "DesktopThemeProbe.h"
#include "Theme.h"

namespace ned::ui {

// ProbeDesktopTheme, run at most once per process. The probe spawns
// subprocesses (gdbus/busctl/gsettings) and reads kdeglobals, which is
// cheap enough to do unconditionally at startup and wrong to repeat on
// every call -- and the answer is a fact about the desktop
// session, which does not change under us in a way worth re-reading mid-
// edit. Startup called it twice (once for the base theme, once for the
// accent); both now share this.
[[nodiscard]] std::optional<DesktopThemeInfo> DesktopTheme();

// Clears the named-paint and surface-override registries and re-parses
// every spec Editor/ThemeSetting.h has accumulated against `theme`, in
// insertion order (named paints first -- a surface's spec may reference one
// by name, and a later registration of the same name wins, matching Janet's
// own sequential evaluation).
//
// Returns the number of specs that failed to parse or named an unknown
// surface part; the caller decides whether that is worth reporting.
int ApplyPaintOverrides(const Theme& theme);

struct ResolvedTheme {
    Theme theme;
    // Empty when everything resolved. Otherwise one line naming what did
    // not -- an unknown theme name, unrecognized ned/theme-set keys, or
    // unparseable paint specs -- for the caller's status line.
    std::string message;
};

// The whole pipeline. Base precedence: `ned/set-theme` from whichever
// init.janet named one (a project's `<root>/.ned/init.janet` loads after the
// global one, so it wins), then the desktop probe, else DarkTheme(). An
// unresolvable name falls through to the next step rather than aborting.
//
// Nothing here reads editor state: a theme is an explicit setting, so the
// only thing that decides it is what a config file says.
//
// Also sets the detected accent and the assumed background (Docs/
// Translucency.md's assumedBackground rule -- what a translucent overlay
// composites against when the theme's own background is the terminal's).
// `ignoreConfiguredName` skips the ned/set-theme step, answering "what would
// this editor look like with no theme configured at all?" -- the picker's
// "None (detect)" row.
[[nodiscard]] ResolvedTheme ResolveConfiguredTheme(bool ignoreConfiguredName = false);

} // namespace ned::ui

#endif // NED_UI_THEMERESOLVE_H
