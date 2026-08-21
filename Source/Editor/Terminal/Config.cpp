#include "Config.h"

#include <algorithm>
#include <mutex>

namespace ned::editor::terminal {

namespace {

    std::mutex g_configMutex;
    int        g_terminalHeightPercent = 40;

} // namespace

void SetTerminalHeightPercent(int percent) {
    const std::lock_guard<std::mutex> lock(g_configMutex);
    g_terminalHeightPercent = std::clamp(percent, 10, 90);
}

int TerminalHeightPercent() {
    const std::lock_guard<std::mutex> lock(g_configMutex);
    return g_terminalHeightPercent;
}

} // namespace ned::editor::terminal
