#include "Registry.h"

#include <algorithm>
#include <map>
#include <mutex>
#include <utility>

namespace ned::editor::tracker {

namespace {

    struct State {
        std::mutex                                             mutex;
        std::map<std::string, std::shared_ptr<const Provider>> providers;
        std::map<std::string, Connection>                      connections;
        // Declaration order is rail order.
        std::vector<Panel> panels;
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

void ClearRegistry() {
    State&                state = Registry();
    const std::lock_guard lock(state.mutex);
    state.providers.clear();
    state.connections.clear();
    state.panels.clear();
}

} // namespace ned::editor::tracker
