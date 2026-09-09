#include "BridgeSetting.h"

#include <mutex>

namespace ned::editor::mcp {

namespace {

    std::mutex& SettingMutex() {
        static std::mutex mutex;
        return mutex;
    }

    bool& SettingStorage() {
        static bool enabled = true;
        return enabled;
    }

} // namespace

void SetAcpMcpBridgeEnabled(bool enabled) {
    const std::lock_guard lock(SettingMutex());
    SettingStorage() = enabled;
}

bool McpBridgeEnabled() {
    const std::lock_guard lock(SettingMutex());
    return SettingStorage();
}

} // namespace ned::editor::mcp
