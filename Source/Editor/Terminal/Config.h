//
// Terminal-panel display settings -- one process-wide choice, mutex-guarded
// static state, mirroring TabWidth.h/ProjectRoot.h's exact pattern.
// Configured from Janet (ned/set-terminal-height-percent); nothing built-in
// changes it from the default.
//

#ifndef NED_EDITOR_TERMINAL_CONFIG_H
#define NED_EDITOR_TERMINAL_CONFIG_H

namespace ned::editor::terminal {

// Percentage of the terminal's height the drawer-style panel covers.
// Defaults to 40; clamped to [10, 90] (a sliver-sized or screen-swallowing
// drawer is never useful).
void              SetTerminalHeightPercent(int percent);
[[nodiscard]] int TerminalHeightPercent();

} // namespace ned::editor::terminal

#endif // NED_EDITOR_TERMINAL_CONFIG_H
