#include "StickyScrollSettings.h"

#include <algorithm>
#include <mutex>

namespace ned::editor {

namespace {

    std::mutex& SettingsMutex() {
        static std::mutex mutex;
        return mutex;
    }

    bool& EnabledStorage() {
        static bool enabled = true;
        return enabled;
    }

    int& MaxRowsStorage() {
        static int rows = 3;
        return rows;
    }

} // namespace

void SetStickyScrollEnabled(bool enabled) {
    const std::lock_guard<std::mutex> lock(SettingsMutex());
    EnabledStorage() = enabled;
}

bool StickyScrollEnabled() {
    const std::lock_guard<std::mutex> lock(SettingsMutex());
    return EnabledStorage();
}

void SetStickyScrollMaxRows(int rows) {
    const std::lock_guard<std::mutex> lock(SettingsMutex());
    MaxRowsStorage() = std::max(0, rows); // 0 is a valid "effectively off" value, never negative
}

int StickyScrollMaxRows() {
    const std::lock_guard<std::mutex> lock(SettingsMutex());
    return MaxRowsStorage();
}

} // namespace ned::editor
