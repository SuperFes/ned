#include "AutoFormatOnSave.h"

#include <mutex>

namespace ned::editor {

namespace {

    std::mutex& AutoFormatOnSaveMutex() {
        static std::mutex mutex;
        return mutex;
    }

    bool& AutoFormatOnSaveStorage() {
        static bool enabled = false;
        return enabled;
    }

} // namespace

void SetAutoFormatOnSave(bool enabled) {
    const std::lock_guard<std::mutex> lock(AutoFormatOnSaveMutex());
    AutoFormatOnSaveStorage() = enabled;
}

bool AutoFormatOnSaveEnabled() {
    const std::lock_guard<std::mutex> lock(AutoFormatOnSaveMutex());
    return AutoFormatOnSaveStorage();
}

} // namespace ned::editor
