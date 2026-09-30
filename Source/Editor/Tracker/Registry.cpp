#include "Registry.h"

#include <algorithm>
#include <map>
#include <mutex>
#include <set>
#include <utility>

namespace ned::editor::tracker {

namespace {

    struct State {
        std::mutex                                             mutex;
        std::map<std::string, std::shared_ptr<const Provider>> providers;
        std::map<std::string, Connection>                      connections;
        // Declaration order is rail order.
        std::vector<Panel>                        panels;
        std::map<std::string, std::vector<Issue>> issuesByPanel;
        std::map<std::string, std::vector<Issue>>       mineByConnection;
        std::map<std::string, std::vector<std::string>> projectKeysByConnection;
        bool                                      autoDetect = true;
        std::uint64_t                                   revision   = 0;
    };

    State& Registry() {
        static State state;
        return state;
    }

} // namespace

void RegisterProvider(const std::string& name, std::shared_ptr<const Provider> provider) {
    State&                state = Registry();
    const std::lock_guard lock(state.mutex);
    ++state.revision;
    state.providers[name] = std::move(provider);
}

std::shared_ptr<const Provider> FindProvider(const std::string& name) {
    State&                state = Registry();
    const std::lock_guard lock(state.mutex);
    const auto            found = state.providers.find(name);
    return found != state.providers.end() ? found->second : nullptr;
}

std::vector<std::shared_ptr<const Provider>> Providers() {
    State&                                       state = Registry();
    const std::lock_guard                        lock(state.mutex);
    std::vector<std::shared_ptr<const Provider>> providers;
    providers.reserve(state.providers.size());
    for (const auto& [name, provider] : state.providers) {
        providers.push_back(provider);
    }
    return providers;
}

void SetConnection(Connection connection) {
    State&                state = Registry();
    const std::lock_guard lock(state.mutex);
    ++state.revision;
    std::string           name = connection.name;
    state.connections.insert_or_assign(std::move(name), std::move(connection));
}

std::optional<Connection> FindConnection(const std::string& name) {
    State&                state = Registry();
    const std::lock_guard lock(state.mutex);
    const auto            found = state.connections.find(name);
    if (found == state.connections.end()) {
        return std::nullopt;
    }
    return found->second;
}

std::vector<Connection> Connections() {
    State&                  state = Registry();
    const std::lock_guard   lock(state.mutex);
    std::vector<Connection> connections;
    connections.reserve(state.connections.size());
    for (const auto& [name, connection] : state.connections) {
        connections.push_back(connection);
    }
    return connections;
}

void AddPanel(Panel panel) {
    State&                state = Registry();
    const std::lock_guard lock(state.mutex);
    ++state.revision;
    const auto            existing = std::ranges::find(state.panels, panel.name, &Panel::name);
    if (existing != state.panels.end()) {
        *existing = std::move(panel);
        return;
    }
    state.panels.push_back(std::move(panel));
}

bool RemovePanel(const std::string& name) {
    State&                state = Registry();
    const std::lock_guard lock(state.mutex);
    ++state.revision;
    state.issuesByPanel.erase(name);
    return std::erase_if(state.panels, [&name](const Panel& panel) { return panel.name == name; }) != 0;
}

std::optional<Panel> FindPanel(const std::string& name) {
    State&                state = Registry();
    const std::lock_guard lock(state.mutex);
    const auto            found = std::ranges::find(state.panels, name, &Panel::name);
    if (found == state.panels.end()) {
        return std::nullopt;
    }
    return *found;
}

std::vector<Panel> Panels() {
    State&                state = Registry();
    const std::lock_guard lock(state.mutex);
    return state.panels;
}

void SetPanelIssues(const std::string& panelName, std::vector<Issue> issues) {
    State&                state = Registry();
    const std::lock_guard lock(state.mutex);
    ++state.revision;
    state.issuesByPanel[panelName] = std::move(issues);
}

std::vector<Issue> PanelIssues(const std::string& panelName) {
    State&                state = Registry();
    const std::lock_guard lock(state.mutex);
    const auto            found = state.issuesByPanel.find(panelName);
    return found != state.issuesByPanel.end() ? found->second : std::vector<Issue>{};
}

std::vector<Issue> KnownIssues() {
    State&                state = Registry();
    const std::lock_guard lock(state.mutex);
    std::vector<Issue>    known;
    std::set<std::string> seen;
    for (const Panel& panel : state.panels) {
        const auto found = state.issuesByPanel.find(panel.name);
        if (found == state.issuesByPanel.end()) {
            continue;
        }
        for (const Issue& issue : found->second) {
            if (seen.insert(issue.key).second) {
                known.push_back(issue);
            }
        }
    }
    return known;
}

std::optional<std::string> ConnectionForIssue(const std::string& key) {
    State&                state = Registry();
    const std::lock_guard lock(state.mutex);
    for (const Panel& panel : state.panels) {
        const auto found = state.issuesByPanel.find(panel.name);
        if (found != state.issuesByPanel.end() && std::ranges::contains(found->second, key, &Issue::key)) {
            return panel.connection;
        }
    }
    for (const auto& [connection, issues] : state.mineByConnection) {
        if (std::ranges::contains(issues, key, &Issue::key)) {
            return connection;
        }
    }
    return std::nullopt;
}

void SetMineIssues(const std::string& connection, std::vector<Issue> issues) {
    State&                state = Registry();
    const std::lock_guard lock(state.mutex);
    ++state.revision;
    state.mineByConnection[connection] = std::move(issues);
}

std::vector<Issue> MineIssues() {
    State&                state = Registry();
    const std::lock_guard lock(state.mutex);
    std::vector<Issue>    mine;
    std::set<std::string> seen;
    for (const auto& [connection, issues] : state.mineByConnection) {
        for (const Issue& issue : issues) {
            if (seen.insert(issue.key).second) {
                mine.push_back(issue);
            }
        }
    }
    return mine;
}

void SetProjectKeys(const std::string& connection, std::vector<std::string> keys) {
    State&                state = Registry();
    const std::lock_guard lock(state.mutex);
    ++state.revision;
    state.projectKeysByConnection[connection] = std::move(keys);
}

namespace {

    // "DEV" of DEV-12; empty for #12.
    std::string ProjectKeyOf(const std::string& issueKey) {
        const std::size_t dash = issueKey.rfind('-');
        return (dash == std::string::npos || dash == 0 || issueKey.starts_with('#')) ? std::string() : issueKey.substr(0, dash);
    }

} // namespace

std::set<std::string> KnownProjectKeys() {
    State&                state = Registry();
    const std::lock_guard lock(state.mutex);
    std::set<std::string> keys;
    for (const auto& [connection, listed] : state.projectKeysByConnection) {
        keys.insert(listed.begin(), listed.end());
    }
    for (const auto* cache : {&state.issuesByPanel, &state.mineByConnection}) {
        for (const auto& [owner, issues] : *cache) {
            for (const Issue& issue : issues) {
                if (std::string project = ProjectKeyOf(issue.key); !project.empty()) {
                    keys.insert(std::move(project));
                }
            }
        }
    }
    return keys;
}

std::optional<std::string> ConnectionForProjectKey(const std::string& projectKey) {
    State&                state = Registry();
    const std::lock_guard lock(state.mutex);
    for (const auto& [connection, listed] : state.projectKeysByConnection) {
        if (std::ranges::contains(listed, projectKey)) {
            return connection;
        }
    }
    for (const auto& [connection, issues] : state.mineByConnection) {
        if (std::ranges::contains(issues, projectKey, [](const Issue& issue) { return ProjectKeyOf(issue.key); })) {
            return connection;
        }
    }
    for (const Panel& panel : state.panels) {
        const auto found = state.issuesByPanel.find(panel.name);
        if (found != state.issuesByPanel.end() &&
            std::ranges::contains(found->second, projectKey, [](const Issue& issue) { return ProjectKeyOf(issue.key); })) {
            return panel.connection;
        }
    }
    return std::nullopt;
}

void SetAutoDetect(bool enabled) {
    State&                state = Registry();
    const std::lock_guard lock(state.mutex);
    ++state.revision;
    state.autoDetect = enabled;
}

bool AutoDetect() {
    State&                state = Registry();
    const std::lock_guard lock(state.mutex);
    return state.autoDetect;
}

std::uint64_t Revision() {
    State&                state = Registry();
    const std::lock_guard lock(state.mutex);
    return state.revision;
}

void ClearRegistry() {
    State&                state = Registry();
    const std::lock_guard lock(state.mutex);
    ++state.revision;
    state.providers.clear();
    state.connections.clear();
    state.panels.clear();
    state.issuesByPanel.clear();
    state.mineByConnection.clear();
    state.projectKeysByConnection.clear();
    state.autoDetect = true;
}

} // namespace ned::editor::tracker
