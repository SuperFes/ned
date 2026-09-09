#include "MultibufferSearchSettings.h"

#include <mutex>

namespace ned::editor {

namespace {

    std::mutex& SettingsMutex() {
        static std::mutex mutex;
        return mutex;
    }

    bool& ScopedSearchStorage() {
        static bool enabled = true;
        return enabled;
    }

} // namespace

void SetMultibufferScopedSearch(bool enabled) {
    const std::lock_guard<std::mutex> lock(SettingsMutex());
    ScopedSearchStorage() = enabled;
}

bool MultibufferScopedSearch() {
    const std::lock_guard<std::mutex> lock(SettingsMutex());
    return ScopedSearchStorage();
}

} // namespace ned::editor
