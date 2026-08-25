#include "WhichKeySettings.h"

#include <mutex>

namespace ned::editor {

namespace {

    std::mutex& EnabledMutex() {
        static std::mutex mutex;
        return mutex;
    }

    bool& EnabledStorage() {
        static bool enabled = true;
        return enabled;
    }

} // namespace

void SetWhichKeyEnabled(bool enabled) {
    const std::lock_guard<std::mutex> lock(EnabledMutex());
    EnabledStorage() = enabled;
}

bool WhichKeyEnabled() {
    const std::lock_guard<std::mutex> lock(EnabledMutex());
    return EnabledStorage();
}

} // namespace ned::editor
