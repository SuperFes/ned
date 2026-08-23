//
// ACP chat panel display settings -- one process-wide choice, mutex-guarded
// static state, mirroring Terminal/Config.h's exact pattern. Configured from
// Janet (ned/set-acp-panel-dock, ned/set-acp-panel-size-percent); nothing
// built-in changes either from its default.
//

#ifndef NED_EDITOR_ACP_ACPPANELCONFIG_H
#define NED_EDITOR_ACP_ACPPANELCONFIG_H

#include <string>

namespace ned::editor::acp {

enum class AcpPanelDock { Bottom,
                          Right };

// "bottom" (default) or "right", case-sensitive; any other value is
// ignored (the current setting is left unchanged) rather than throwing --
// same permissiveness as this subsystem's other string-configured settings.
void                       SetAcpPanelDock(const std::string& side);
[[nodiscard]] AcpPanelDock GetAcpPanelDock();

// Percentage of the terminal's height (dock == Bottom) or width
// (dock == Right) the panel covers. Defaults to 30; clamped to [15, 70].
void              SetAcpPanelSizePercent(int percent);
[[nodiscard]] int AcpPanelSizePercent();

} // namespace ned::editor::acp

#endif // NED_EDITOR_ACP_ACPPANELCONFIG_H
