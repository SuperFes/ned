//
// ACP chat panel display settings -- one process-wide choice, mutex-guarded
// static state, mirroring Terminal/Config.h's exact pattern. Configured from
// Janet (ned/set-acp-panel-dock, ned/set-acp-panel-size-percent,
// ned/set-acp-tool-calls, ned/set-acp-thinking); nothing built-in changes
// any of them from its default. Unrecognized strings leave a setting
// unchanged.
//

#ifndef NED_EDITOR_ACP_PANELCONFIG_H
#define NED_EDITOR_ACP_PANELCONFIG_H

#include <string>

namespace ned::editor::acp {

enum class PanelDock { Bottom,
                          Right };

// "bottom" (default) or "right", case-sensitive; any other value is
// ignored (the current setting is left unchanged) rather than throwing --
// same permissiveness as this subsystem's other string-configured settings.
void                       SetAcpPanelDock(const std::string& side);
[[nodiscard]] PanelDock GetAcpPanelDock();

// Percentage of the terminal's height (dock == Bottom) or width
// (dock == Right) the panel covers. Defaults to 30; clamped to [15, 70].
void              SetAcpPanelSizePercent(int percent);
[[nodiscard]] int PanelSizePercent();

// How the transcript shows tool calls: one line each until expanded
// ("collapsed", default), or always with their details ("expanded").
enum class ToolCallDisplay { Collapsed,
                             Expanded };
void                          SetAcpToolCallDisplay(const std::string& display);
[[nodiscard]] ToolCallDisplay GetAcpToolCallDisplay();

// How the transcript shows the agent's thinking: a one-line summary until
// expanded ("collapsed", default), in full ("expanded"), or not at all
// ("hidden").
enum class ThinkingDisplay { Collapsed,
                             Expanded,
                             Hidden };
void                          SetAcpThinkingDisplay(const std::string& display);
[[nodiscard]] ThinkingDisplay GetAcpThinkingDisplay();

} // namespace ned::editor::acp

#endif // NED_EDITOR_ACP_PANELCONFIG_H
