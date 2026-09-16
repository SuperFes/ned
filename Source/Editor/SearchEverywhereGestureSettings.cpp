#include "SearchEverywhereGestureSettings.h"

#include <mutex>

namespace ned::editor {

namespace {

    std::mutex& SettingsMutex() {
        static std::mutex mutex;
        return mutex;
    }

    bool& GestureEnabledStorage() {
        static bool enabled = true;
        return enabled;
    }

} // namespace

void SetSearchEverywhereGestureEnabled(bool enabled) {
    const std::lock_guard<std::mutex> lock(SettingsMutex());
    GestureEnabledStorage() = enabled;
}

bool SearchEverywhereGestureEnabled() {
    const std::lock_guard<std::mutex> lock(SettingsMutex());
    return GestureEnabledStorage();
}

} // namespace ned::editor
