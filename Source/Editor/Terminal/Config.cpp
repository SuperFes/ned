#include "Config.h"

#include <algorithm>
#include <mutex>

namespace ned::editor::terminal {

namespace {

    std::mutex g_configMutex;
    int        g_terminalHeightPercent = 40;
    bool       g_applicationTitles     = true;

} // namespace

void SetTerminalHeightPercent(int percent) {
    const std::lock_guard<std::mutex> lock(g_configMutex);
    g_terminalHeightPercent = std::clamp(percent, 10, 90);
}

int TerminalHeightPercent() {
    const std::lock_guard<std::mutex> lock(g_configMutex);
    return g_terminalHeightPercent;
}

void SetTerminalApplicationTitlesEnabled(bool enabled) {
    const std::lock_guard<std::mutex> lock(g_configMutex);
    g_applicationTitles = enabled;
}

bool TerminalApplicationTitlesEnabled() {
    const std::lock_guard<std::mutex> lock(g_configMutex);
    return g_applicationTitles;
}

} // namespace ned::editor::terminal
