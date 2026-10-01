//
// Terminal-panel display settings -- one process-wide choice, mutex-guarded
// static state, mirroring TabWidth.h/ProjectRoot.h's exact pattern.
// Configured from Janet (ned/set-terminal-*); nothing built-in changes them
// from their defaults.
//

#ifndef NED_EDITOR_TERMINAL_CONFIG_H
#define NED_EDITOR_TERMINAL_CONFIG_H

namespace ned::editor::terminal {

// Percentage of the terminal's height the drawer-style panel covers.
// Defaults to 40; clamped to [10, 90] (a sliver-sized or screen-swallowing
// drawer is never useful).
void              SetTerminalHeightPercent(int percent);
[[nodiscard]] int TerminalHeightPercent();

// Whether a title an application sets (OSC 0/2) names the terminal's tab.
// Defaults to true; false keeps the panel's static label.
void               SetTerminalApplicationTitlesEnabled(bool enabled);
[[nodiscard]] bool TerminalApplicationTitlesEnabled();

} // namespace ned::editor::terminal

#endif // NED_EDITOR_TERMINAL_CONFIG_H
