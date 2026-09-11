#include "ImportFixupSettings.h"

#include <mutex>

namespace ned::editor {

namespace {

    std::mutex& SettingsMutex() {
        static std::mutex mutex;
        return mutex;
    }

    bool& ImportFixupStorage() {
        static bool enabled = true;
        return enabled;
    }

    std::size_t& ImportFixupMaxFilesStorage() {
        static std::size_t limit = 20000;
        return limit;
    }

} // namespace

void SetImportFixupEnabled(bool enabled) {
    const std::lock_guard<std::mutex> lock(SettingsMutex());
    ImportFixupStorage() = enabled;
}

bool ImportFixupEnabled() {
    const std::lock_guard<std::mutex> lock(SettingsMutex());
    return ImportFixupStorage();
}

void SetImportFixupMaxFiles(std::size_t limit) {
    const std::lock_guard<std::mutex> lock(SettingsMutex());
    ImportFixupMaxFilesStorage() = limit;
}

std::size_t ImportFixupMaxFiles() {
    const std::lock_guard<std::mutex> lock(SettingsMutex());
    return ImportFixupMaxFilesStorage();
}

} // namespace ned::editor
