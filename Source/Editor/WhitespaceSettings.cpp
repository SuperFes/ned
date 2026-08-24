#include "WhitespaceSettings.h"

#include <mutex>

namespace ned::editor {

namespace {

    std::mutex& SettingsMutex() {
        static std::mutex mutex;
        return mutex;
    }

    bool& TrailingWhitespaceStorage() {
        static bool enabled = false;
        return enabled;
    }

    bool& IndentGuidesStorage() {
        static bool enabled = false;
        return enabled;
    }

} // namespace

void SetTrailingWhitespaceHighlightEnabled(bool enabled) {
    const std::lock_guard<std::mutex> lock(SettingsMutex());
    TrailingWhitespaceStorage() = enabled;
}

bool TrailingWhitespaceHighlightEnabled() {
    const std::lock_guard<std::mutex> lock(SettingsMutex());
    return TrailingWhitespaceStorage();
}

void SetIndentGuidesEnabled(bool enabled) {
    const std::lock_guard<std::mutex> lock(SettingsMutex());
    IndentGuidesStorage() = enabled;
}

bool IndentGuidesEnabled() {
    const std::lock_guard<std::mutex> lock(SettingsMutex());
    return IndentGuidesStorage();
}

} // namespace ned::editor
