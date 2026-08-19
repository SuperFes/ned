#include "TaskConfig.h"

#include <mutex>
#include <unordered_map>
#include <utility>

namespace ned::editor::tasks {

namespace {

    std::mutex                                                g_mutex;
    std::unordered_map<std::string, std::vector<std::string>> g_commands;

} // namespace

void SetTaskCommand(const std::string& name, std::vector<std::string> argv) {
    const std::lock_guard<std::mutex> lock(g_mutex);
    if (argv.empty()) {
        g_commands.erase(name);
    }
    else {
        g_commands[name] = std::move(argv);
    }
}

std::optional<std::vector<std::string>> TaskCommand(const std::string& name) {
    const std::lock_guard<std::mutex> lock(g_mutex);
    const auto                        it = g_commands.find(name);
    if (it == g_commands.end()) {
        return std::nullopt;
    }
    return it->second;
}

} // namespace ned::editor::tasks
