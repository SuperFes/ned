#include "RulerSettings.h"

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

    int& ColumnStorage() {
        static int column = 80;
        return column;
    }

} // namespace

void SetRulerEnabled(bool enabled) {
    const std::lock_guard<std::mutex> lock(SettingsMutex());
    EnabledStorage() = enabled;
}

bool RulerEnabled() {
    const std::lock_guard<std::mutex> lock(SettingsMutex());
    return EnabledStorage();
}

void SetRulerColumn(int column) {
    const std::lock_guard<std::mutex> lock(SettingsMutex());
    ColumnStorage() = std::max(1, column);
}

int RulerColumn() {
    const std::lock_guard<std::mutex> lock(SettingsMutex());
    return ColumnStorage();
}

} // namespace ned::editor
