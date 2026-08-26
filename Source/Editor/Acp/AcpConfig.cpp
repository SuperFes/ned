#include "AcpConfig.h"

#include <algorithm>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace ned::editor::acp {

namespace {

    std::mutex                                                g_mutex;
    std::unordered_map<std::string, std::vector<std::string>> g_commands;

} // namespace

void SetAcpAgentCommand(const std::string& name, std::vector<std::string> argv) {
    const std::lock_guard<std::mutex> lock(g_mutex);
    if (argv.empty()) {
        g_commands.erase(name);
    }
    else {
        g_commands[name] = std::move(argv);
    }
}

std::optional<std::vector<std::string>> AcpAgentCommand(const std::string& name) {
    const std::lock_guard<std::mutex> lock(g_mutex);
    const auto                        it = g_commands.find(name);
    if (it == g_commands.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::vector<std::string> AcpAgentNames() {
    const std::lock_guard<std::mutex> lock(g_mutex);
    std::vector<std::string>          names;
    names.reserve(g_commands.size());
    for (const auto& [name, argv] : g_commands) {
        names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

} // namespace ned::editor::acp
