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
        bool                                      autoDetect = true;
    };

    State& Registry() {
        static State state;
        return state;
    }

} // namespace

void RegisterProvider(const std::string& name, std::shared_ptr<const Provider> provider) {
    State&                state = Registry();
    const std::lock_guard lock(state.mutex);
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

void AddPanel(Panel panel) {
    State&                state = Registry();
    const std::lock_guard lock(state.mutex);
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

void SetAutoDetect(bool enabled) {
    State&                state = Registry();
    const std::lock_guard lock(state.mutex);
    state.autoDetect = enabled;
}

bool AutoDetect() {
    State&                state = Registry();
    const std::lock_guard lock(state.mutex);
    return state.autoDetect;
}

void ClearRegistry() {
    State&                state = Registry();
    const std::lock_guard lock(state.mutex);
    state.providers.clear();
    state.connections.clear();
    state.panels.clear();
    state.issuesByPanel.clear();
    state.autoDetect = true;
}

} // namespace ned::editor::tracker
