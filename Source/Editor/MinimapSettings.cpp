#include "MinimapSettings.h"

#include <algorithm>
#include <mutex>

namespace ned::editor {

namespace {

    std::mutex& SettingsMutex() {
        static std::mutex mutex;
        return mutex;
    }

    bool& EnabledStorage() {
        static bool enabled = true;
        return enabled;
    }

    int& WidthStorage() {
        static int width = 5;
        return width;
    }

    int& CharsPerDotStorage() {
        static int charsPerDot = 8;
        return charsPerDot;
    }

} // namespace

void SetMinimapEnabled(bool enabled) {
    const std::lock_guard<std::mutex> lock(SettingsMutex());
    EnabledStorage() = enabled;
}

bool MinimapEnabled() {
    const std::lock_guard<std::mutex> lock(SettingsMutex());
    return EnabledStorage();
}

void SetMinimapWidth(int columns) {
    const std::lock_guard<std::mutex> lock(SettingsMutex());
    WidthStorage() = std::max(1, columns);
}

int MinimapWidth() {
    const std::lock_guard<std::mutex> lock(SettingsMutex());
    return WidthStorage();
}

void SetMinimapCharsPerDot(int columns) {
    const std::lock_guard<std::mutex> lock(SettingsMutex());
    CharsPerDotStorage() = std::max(1, columns);
}

int MinimapCharsPerDot() {
    const std::lock_guard<std::mutex> lock(SettingsMutex());
    return CharsPerDotStorage();
}

} // namespace ned::editor
