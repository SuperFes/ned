#include "ProseChecker.h"

#include <mutex>

#include "Editor/Process/ChildProcess.h"

namespace ned::editor::lsp {

namespace {

    std::mutex                                g_overrideMutex;
    std::optional<std::vector<std::string>>   g_override; // nullopt -> auto-detect

    std::mutex g_enabledMutex;
    bool       g_enabled = true;

    // Memoized separately from g_override: an unset override should still
    // only scan $PATH once per process, not once per ProseCheckerCommand()
    // call.
    std::mutex                                g_autoDetectMutex;
    bool                                       g_autoDetectResolved = false;
    std::optional<std::vector<std::string>>   g_autoDetected;

    std::optional<std::vector<std::string>> AutoDetect() {
        const std::lock_guard<std::mutex> lock(g_autoDetectMutex);
        if (!g_autoDetectResolved) {
            g_autoDetected =
                process::ResolveExecutable("harper-ls") ? std::make_optional(std::vector<std::string>{"harper-ls", "--stdio"}) : std::nullopt;
            g_autoDetectResolved = true;
        }
        return g_autoDetected;
    }

} // namespace

void SetProseCheckerCommand(std::vector<std::string> argv) {
    const std::lock_guard<std::mutex> lock(g_overrideMutex);
    if (argv.empty()) {
        g_override.reset();
    }
    else {
        g_override = std::move(argv);
    }
}

std::optional<std::vector<std::string>> ProseCheckerCommand() {
    if (!ProseCheckingEnabled()) {
        return std::nullopt;
    }
    {
        const std::lock_guard<std::mutex> lock(g_overrideMutex);
        if (g_override) {
            return g_override;
        }
    }
    return AutoDetect();
}

void SetProseCheckingEnabled(bool enabled) {
    const std::lock_guard<std::mutex> lock(g_enabledMutex);
    g_enabled = enabled;
}

bool ProseCheckingEnabled() {
    const std::lock_guard<std::mutex> lock(g_enabledMutex);
    return g_enabled;
}

} // namespace ned::editor::lsp
