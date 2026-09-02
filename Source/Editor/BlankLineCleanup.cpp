#include "BlankLineCleanup.h"

#include <mutex>

namespace ned::editor {

namespace {

    std::mutex& CleanBlankLineMutex() {
        static std::mutex mutex;
        return mutex;
    }

    bool& CleanBlankLineStorage() {
        static bool enabled = true;
        return enabled;
    }

} // namespace

void SetCleanBlankLineOnNewline(bool enabled) {
    const std::lock_guard<std::mutex> lock(CleanBlankLineMutex());
    CleanBlankLineStorage() = enabled;
}

bool CleanBlankLineOnNewline() {
    const std::lock_guard<std::mutex> lock(CleanBlankLineMutex());
    return CleanBlankLineStorage();
}

} // namespace ned::editor
