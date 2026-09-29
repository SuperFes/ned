#include "PanelConfig.h"

#include <algorithm>
#include <mutex>

namespace ned::editor::acp {

namespace {
    std::mutex   g_configMutex;
    PanelDock g_dock        = PanelDock::Bottom;
    int          g_sizePercent = 30;
    ToolCallDisplay g_toolCalls   = ToolCallDisplay::Collapsed;
    ThinkingDisplay g_thinking    = ThinkingDisplay::Collapsed;
} // namespace

void SetAcpPanelDock(const std::string& side) {
    const std::lock_guard<std::mutex> lock(g_configMutex);
    if (side == "bottom") {
        g_dock = PanelDock::Bottom;
    }
    else if (side == "right") {
        g_dock = PanelDock::Right;
    }
    // else: unrecognized -- leave the current setting unchanged.
}

PanelDock GetAcpPanelDock() {
    const std::lock_guard<std::mutex> lock(g_configMutex);
    return g_dock;
}

void SetAcpPanelSizePercent(int percent) {
    const std::lock_guard<std::mutex> lock(g_configMutex);
    g_sizePercent = std::clamp(percent, 15, 70);
}

int PanelSizePercent() {
    const std::lock_guard<std::mutex> lock(g_configMutex);
    return g_sizePercent;
}

void SetAcpToolCallDisplay(const std::string& display) {
    const std::lock_guard<std::mutex> lock(g_configMutex);
    if (display == "collapsed") {
        g_toolCalls = ToolCallDisplay::Collapsed;
    }
    else if (display == "expanded") {
        g_toolCalls = ToolCallDisplay::Expanded;
    }
}

ToolCallDisplay GetAcpToolCallDisplay() {
    const std::lock_guard<std::mutex> lock(g_configMutex);
    return g_toolCalls;
}

void SetAcpThinkingDisplay(const std::string& display) {
    const std::lock_guard<std::mutex> lock(g_configMutex);
    if (display == "collapsed") {
        g_thinking = ThinkingDisplay::Collapsed;
    }
    else if (display == "expanded") {
        g_thinking = ThinkingDisplay::Expanded;
    }
    else if (display == "hidden") {
        g_thinking = ThinkingDisplay::Hidden;
    }
}

ThinkingDisplay GetAcpThinkingDisplay() {
    const std::lock_guard<std::mutex> lock(g_configMutex);
    return g_thinking;
}

} // namespace ned::editor::acp
