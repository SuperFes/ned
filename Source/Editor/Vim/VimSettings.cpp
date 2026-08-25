#include "VimSettings.h"

#include <mutex>

namespace ned::editor::vim {

namespace {

    std::mutex& EnabledMutex() {
        static std::mutex mutex;
        return mutex;
    }

    bool& EnabledStorage() {
        static bool enabled = false;
        return enabled;
    }

} // namespace

void SetVimModeEnabled(bool enabled) {
    const std::lock_guard<std::mutex> lock(EnabledMutex());
    EnabledStorage() = enabled;
}

bool VimModeEnabled() {
    const std::lock_guard<std::mutex> lock(EnabledMutex());
    return EnabledStorage();
}

} // namespace ned::editor::vim
