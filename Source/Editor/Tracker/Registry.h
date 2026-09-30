//
// Process-wide tracker providers, connections and panels. Mutex-guarded
// static state like Vcs/ProviderRegistry.h. Names are resolved when a
// panel is fetched, not when it is declared, so init.janet can declare
// panels, connections and providers in any order.
//

#ifndef NED_EDITOR_TRACKER_REGISTRY_H
#define NED_EDITOR_TRACKER_REGISTRY_H

#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "Provider.h"

namespace ned::editor::tracker {

// Re-registering a name replaces it; a plugin reloading is expected use.
void                                          RegisterProvider(const std::string& name, std::shared_ptr<const Provider> provider);
[[nodiscard]] std::shared_ptr<const Provider> FindProvider(const std::string& name);
// Every registered provider, ordered by name.
[[nodiscard]] std::vector<std::shared_ptr<const Provider>> Providers();

void                                    SetConnection(Connection connection);
[[nodiscard]] std::optional<Connection> FindConnection(const std::string& name);
// Ordered by name.
[[nodiscard]] std::vector<Connection> Connections();

// Re-adding a name replaces that panel in place, keeping its position.
void                               AddPanel(Panel panel);
bool                               RemovePanel(const std::string& name);
[[nodiscard]] std::optional<Panel> FindPanel(const std::string& name);
[[nodiscard]] std::vector<Panel>   Panels();

// The last successful fetch of each panel, so anything that wants "issues
// I've seen" (an issue-key picker) needn't refetch. Removing a panel drops
// its issues.
void                             SetPanelIssues(const std::string& panelName, std::vector<Issue> issues);
[[nodiscard]] std::vector<Issue> PanelIssues(const std::string& panelName);
// Every cached issue once, first occurrence wins, in panel order.
[[nodiscard]] std::vector<Issue> KnownIssues();
// The connection of the first panel, in panel order, whose cached issues
// include key, else of the first connection whose own issues (below) do;
// nullopt when no fetch has seen it.
[[nodiscard]] std::optional<std::string> ConnectionForIssue(const std::string& key);

// Per connection, for issue-key completion: the issues assigned to or
// watched by the user, and the project keys ("DEV") its issue keys start
// with. Replaced wholesale by each fetch.
void                             SetMineIssues(const std::string& connection, std::vector<Issue> issues);
[[nodiscard]] std::vector<Issue> MineIssues(); // every connection's, by connection name, first occurrence wins
void                             SetProjectKeys(const std::string& connection, std::vector<std::string> keys);
// Every project key a connection listed or a fetched issue's key showed.
[[nodiscard]] std::set<std::string> KnownProjectKeys();
// The connection that listed projectKey, else the one a fetched issue with
// that prefix came through.
[[nodiscard]] std::optional<std::string> ConnectionForProjectKey(const std::string& projectKey);

// Whether providers may add panels for the project's git remotes at
// startup. On by default.
void               SetAutoDetect(bool enabled);
[[nodiscard]] bool AutoDetect();

// Bumped by every change above, so something derived from the registry can
// tell when to derive it again.
[[nodiscard]] std::uint64_t Revision();

// Test-only: empties providers, connections, panels and cached issues,
// and turns auto-detect back on.
void ClearRegistry();

} // namespace ned::editor::tracker

#endif // NED_EDITOR_TRACKER_REGISTRY_H
