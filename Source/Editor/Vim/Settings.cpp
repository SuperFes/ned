#include "Settings.h"

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

    bool& CommandCompletionStorage() {
        static bool enabled = true;
        return enabled;
    }

} // namespace

void SetModeEnabled(bool enabled) {
    const std::lock_guard<std::mutex> lock(EnabledMutex());
    EnabledStorage() = enabled;
}

bool ModeEnabled() {
    const std::lock_guard<std::mutex> lock(EnabledMutex());
    return EnabledStorage();
}

void SetCommandCompletionEnabled(bool enabled) {
    const std::lock_guard<std::mutex> lock(EnabledMutex());
    CommandCompletionStorage() = enabled;
}

bool CommandCompletionEnabled() {
    const std::lock_guard<std::mutex> lock(EnabledMutex());
    return CommandCompletionStorage();
}

} // namespace ned::editor::vim
