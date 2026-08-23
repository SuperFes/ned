#include "AcpPanelConfig.h"

#include <algorithm>
#include <mutex>

namespace ned::editor::acp {

namespace {
    std::mutex   g_configMutex;
    AcpPanelDock g_dock        = AcpPanelDock::Bottom;
    int          g_sizePercent = 30;
} // namespace

void SetAcpPanelDock(const std::string& side) {
    const std::lock_guard<std::mutex> lock(g_configMutex);
    if (side == "bottom") {
        g_dock = AcpPanelDock::Bottom;
    }
    else if (side == "right") {
        g_dock = AcpPanelDock::Right;
    }
    // else: unrecognized -- leave the current setting unchanged.
}

AcpPanelDock GetAcpPanelDock() {
    const std::lock_guard<std::mutex> lock(g_configMutex);
    return g_dock;
}

void SetAcpPanelSizePercent(int percent) {
    const std::lock_guard<std::mutex> lock(g_configMutex);
    g_sizePercent = std::clamp(percent, 15, 70);
}

int AcpPanelSizePercent() {
    const std::lock_guard<std::mutex> lock(g_configMutex);
    return g_sizePercent;
}

} // namespace ned::editor::acp
