#include "ChangeSignatureSettings.h"

#include <mutex>

namespace ned::editor {

namespace {

    std::mutex& SettingsMutex() {
        static std::mutex mutex;
        return mutex;
    }

    std::size_t& ChangeSignatureMaxFilesStorage() {
        static std::size_t limit = 20000;
        return limit;
    }

} // namespace

void SetChangeSignatureMaxFiles(std::size_t limit) {
    const std::lock_guard<std::mutex> lock(SettingsMutex());
    ChangeSignatureMaxFilesStorage() = limit;
}

std::size_t ChangeSignatureMaxFiles() {
    const std::lock_guard<std::mutex> lock(SettingsMutex());
    return ChangeSignatureMaxFilesStorage();
}

} // namespace ned::editor
