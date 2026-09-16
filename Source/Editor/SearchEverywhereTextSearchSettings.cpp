#include "SearchEverywhereTextSearchSettings.h"

#include <mutex>

namespace ned::editor {

namespace {

    std::mutex& SettingsMutex() {
        static std::mutex mutex;
        return mutex;
    }

    bool& TextSearchEnabledStorage() {
        static bool enabled = true;
        return enabled;
    }

} // namespace

void SetSearchEverywhereTextSearchEnabled(bool enabled) {
    const std::lock_guard<std::mutex> lock(SettingsMutex());
    TextSearchEnabledStorage() = enabled;
}

bool SearchEverywhereTextSearchEnabled() {
    const std::lock_guard<std::mutex> lock(SettingsMutex());
    return TextSearchEnabledStorage();
}

} // namespace ned::editor
