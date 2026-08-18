#include "LspServerConfig.h"

#include <mutex>
#include <unordered_map>
#include <utility>

namespace ned::editor::lsp {

namespace {

    std::mutex                                                g_mutex;
    std::unordered_map<std::string, std::vector<std::string>> g_commands;

} // namespace

void SetLspServerCommand(const std::string& language, std::vector<std::string> argv) {
    const std::lock_guard<std::mutex> lock(g_mutex);
    if (argv.empty()) {
        g_commands.erase(language);
    }
    else {
        g_commands[language] = std::move(argv);
    }
}

std::optional<std::vector<std::string>> LspServerCommand(const std::string& language) {
    const std::lock_guard<std::mutex> lock(g_mutex);
    const auto                        it = g_commands.find(language);
    if (it == g_commands.end()) {
        return std::nullopt;
    }
    return it->second;
}

} // namespace ned::editor::lsp
