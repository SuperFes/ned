//
// Process-wide tracker providers, connections and panels. Mutex-guarded
// static state like Vcs/ProviderRegistry.h. Names are resolved when a
// panel is fetched, not when it is declared, so init.janet can declare
// panels, connections and providers in any order.
//

#ifndef NED_EDITOR_TRACKER_REGISTRY_H
#define NED_EDITOR_TRACKER_REGISTRY_H

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "Provider.h"

namespace ned::editor::tracker {

// Re-registering a name replaces it; a plugin reloading is expected use.
void                                          RegisterProvider(const std::string& name, std::shared_ptr<const Provider> provider);
[[nodiscard]] std::shared_ptr<const Provider> FindProvider(const std::string& name);

void                                    SetConnection(Connection connection);
[[nodiscard]] std::optional<Connection> FindConnection(const std::string& name);

// Re-adding a name replaces that panel in place, keeping its position.
void                               AddPanel(Panel panel);
bool                               RemovePanel(const std::string& name);
[[nodiscard]] std::optional<Panel> FindPanel(const std::string& name);
[[nodiscard]] std::vector<Panel>   Panels();

// Test-only: empties providers, connections and panels.
void ClearRegistry();

} // namespace ned::editor::tracker

#endif // NED_EDITOR_TRACKER_REGISTRY_H
