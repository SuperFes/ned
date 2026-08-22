//
// Desktop-environment theme detection (theme-polish follow-up, Phase 4) --
// the theme-precedence chain's last-resort fallback (see main.cpp), tried
// only once every explicit source (a remembered theme variable,
// `ned/set-theme`, an existing `ned --detect-theme` terminal-probe file)
// has already come up empty. Unlike TerminalColorProbe.h (which queries the
// *terminal* directly over OSC and needs raw-mode stdin), this queries the
// *desktop environment* the terminal happens to be running under, so it's
// safe to run unconditionally at any point in startup -- no terminal state
// to fight over.
//
// Two independent facts are sought: light/dark polarity and an accent
// color, tried via a short waterfall of desktop-specific mechanisms (see
// the .cpp for each one's own comment):
//   1. The freedesktop `org.freedesktop.appearance` portal setting, over
//      D-Bus via whichever of `gdbus`/`busctl` is on $PATH -- works
//      uniformly across GNOME and any Plasma version new enough to ship
//      xdg-desktop-portal-kde's appearance implementation, without this
//      file needing to know which desktop it's actually talking to.
//   2. GNOME's own `gsettings` keys, if $XDG_CURRENT_DESKTOP names GNOME
//      and the portal didn't answer everything.
//   3. A direct parse of `kdeglobals`, if $XDG_CURRENT_DESKTOP names
//      KDE/Plasma and the portal didn't answer everything.
// Every step is best-effort and independently optional -- a subprocess
// that's missing, times out, or returns something unparseable just leaves
// that one fact undetermined rather than failing the whole probe.
//
// Split the same way TerminalColorProbe.h is: the reply-parsing functions
// below are pure and unit-tested directly; ProbeDesktopTheme is the
// impure orchestration (subprocess spawns, file reads) that only a real
// desktop session can meaningfully exercise end-to-end.
//

#ifndef NED_UI_DESKTOPTHEMEPROBE_H
#define NED_UI_DESKTOPTHEMEPROBE_H

#include <optional>
#include <string_view>

#include "Theme.h"

namespace ned::ui {

struct DesktopThemeInfo {
    // Only meaningful when the probe actually determined a polarity --
    // ProbeDesktopTheme only ever returns a DesktopThemeInfo at all when at
    // least one of polarity or accent was determined, so a caller that gets
    // std::nullopt back never needs to consult this default.
    bool                  preferDark = true;
    std::optional<Color>  accent;
};

// freedesktop portal color-scheme reply parsing (both `gdbus`'s
// "<uint32 N>" shape and `busctl`'s "u N" shape reduce to one
// unambiguously-labeled integer token). Maps the portal's documented enum
// (0 = no preference, 1 = prefer dark, 2 = prefer light) to a polarity;
// returns std::nullopt for "no preference," an unparseable reply, or an
// unrecognized value -- an explicit "don't know," not a default.
[[nodiscard]] std::optional<bool> ParsePortalColorScheme(std::string_view reply);

// freedesktop portal accent-color reply parsing: finds the first three
// consecutive 0.0-1.0 floats in the reply (both `gdbus`'s
// "(0.2, 0.4, 0.8)" and `busctl`'s "(ddd) 0.2 0.4 0.8" shapes reduce to
// three plain floats in RGB order) and converts them to an 8-bit-channel
// Color.
[[nodiscard]] std::optional<Color> ParsePortalAccentColor(std::string_view reply);

// GNOME's `gsettings get org.gnome.desktop.interface color-scheme` reply
// (a quoted token: 'prefer-dark'/'prefer-light'/'default'). "default"
// returns std::nullopt (no preference expressed), same as the portal's 0.
[[nodiscard]] std::optional<bool> ParseGsettingsColorScheme(std::string_view reply);

// GNOME's `gsettings get org.gnome.desktop.interface accent-color` reply
// (a quoted named color, e.g. 'blue') mapped through GNOME/Adwaita's fixed,
// documented accent palette. An unrecognized name returns std::nullopt.
[[nodiscard]] std::optional<Color> GnomeAccentColorFromName(std::string_view reply);

struct KdeGlobalsInfo {
    std::optional<bool>  preferDark;
    std::optional<Color> accent;
};

// Parses the content of a KDE `kdeglobals` INI file (passed in as text, not
// read from disk here, so this stays pure/testable). Polarity comes from
// [General]'s ColorScheme= name containing "dark"/"light"
// (case-insensitive) -- KDE's own naming convention for every bundled and
// community color scheme. Accent comes from [General]'s AccentColor=
// (Plasma 6, an explicit "r,g,b" triple), falling back to
// [Colors:Selection]'s BackgroundNormal= (the selection-highlight color,
// which is the accent in practice for every color scheme that doesn't set
// AccentColor explicitly) if that key is absent.
[[nodiscard]] KdeGlobalsInfo ParseKdeGlobals(std::string_view content);

// Runs the full waterfall described above. Never throws; returns
// std::nullopt only if nothing at all could be determined (no D-Bus
// session, none of gdbus/busctl/gsettings on $PATH, no readable
// kdeglobals, or $XDG_CURRENT_DESKTOP names neither GNOME nor KDE) -- the
// caller falls through to whatever it was already going to do (main.cpp
// falls through to DarkTheme()).
[[nodiscard]] std::optional<DesktopThemeInfo> ProbeDesktopTheme();

// DarkTheme()/LightTheme() by polarity, with the accent color (if any)
// applied to the same fields TerminalColorProbe::BuildDetectedTheme applies
// its own single detected accent to (borderAccent, the keyword slot, and
// the focused mode-line gradient pulled 60% toward it) -- same "one
// detected color still produces a coherent-looking theme" derivation, just
// starting from a bundled base instead of the detected background.
[[nodiscard]] Theme BuildDesktopTheme(const DesktopThemeInfo& info);

} // namespace ned::ui

#endif // NED_UI_DESKTOPTHEMEPROBE_H
