#include "ClassFileSyncSettings.h"

#include <mutex>

namespace ned::editor {

namespace {

    std::mutex& SettingsMutex() {
        static std::mutex mutex;
        return mutex;
    }

    bool& ClassFileSyncStorage() {
        static bool enabled = true;
        return enabled;
    }

} // namespace

void SetClassFileSync(bool enabled) {
    const std::lock_guard<std::mutex> lock(SettingsMutex());
    ClassFileSyncStorage() = enabled;
}

bool ClassFileSyncEnabled() {
    const std::lock_guard<std::mutex> lock(SettingsMutex());
    return ClassFileSyncStorage();
}

} // namespace ned::editor
