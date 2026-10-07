#include "IconSettings.h"

#include <mutex>

namespace ned::editor {

namespace {

    std::mutex& SettingsMutex() {
        static std::mutex mutex;
        return mutex;
    }

    bool& FileTypeIconsStorage() {
        static bool enabled = true;
        return enabled;
    }

} // namespace

void SetFileTypeIconsEnabled(bool enabled) {
    const std::lock_guard<std::mutex> lock(SettingsMutex());
    FileTypeIconsStorage() = enabled;
}

bool FileTypeIconsEnabled() {
    const std::lock_guard<std::mutex> lock(SettingsMutex());
    return FileTypeIconsStorage();
}

} // namespace ned::editor
