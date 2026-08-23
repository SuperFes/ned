#include "LspServerConfig.h"

#include <mutex>
#include <unordered_map>
#include <utility>

namespace ned::editor::lsp {

namespace {

    std::mutex                                                g_mutex;
    std::unordered_map<std::string, std::vector<std::string>> g_commands;

    std::mutex g_autoCompleteMutex;
    bool       g_autoCompleteEnabled = true;

    std::mutex g_debounceMutex;
    int        g_completionDebounceMs = 350;

    std::mutex g_diagnosticsDebounceMutex;
    int        g_diagnosticsDebounceMs = 500;

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

void SetLspAutoCompleteEnabled(bool enabled) {
    const std::lock_guard<std::mutex> lock(g_autoCompleteMutex);
    g_autoCompleteEnabled = enabled;
}

bool LspAutoCompleteEnabled() {
    const std::lock_guard<std::mutex> lock(g_autoCompleteMutex);
    return g_autoCompleteEnabled;
}

void SetLspCompletionDebounceMs(int milliseconds) {
    const std::lock_guard<std::mutex> lock(g_debounceMutex);
    g_completionDebounceMs = (milliseconds > 0) ? milliseconds : 1;
}

int LspCompletionDebounceMs() {
    const std::lock_guard<std::mutex> lock(g_debounceMutex);
    return g_completionDebounceMs;
}

void SetLspDiagnosticsDebounceMs(int milliseconds) {
    const std::lock_guard<std::mutex> lock(g_diagnosticsDebounceMutex);
    g_diagnosticsDebounceMs = (milliseconds > 0) ? milliseconds : 1;
}

int LspDiagnosticsDebounceMs() {
    const std::lock_guard<std::mutex> lock(g_diagnosticsDebounceMutex);
    return g_diagnosticsDebounceMs;
}

} // namespace ned::editor::lsp
