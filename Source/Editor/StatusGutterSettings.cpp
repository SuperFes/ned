#include "StatusGutterSettings.h"

#include <mutex>

namespace ned::editor {

namespace {

    std::mutex& SettingsMutex() {
        static std::mutex mutex;
        return mutex;
    }

    bool& UnsavedChangeSwatchStorage() {
        static bool enabled = true;
        return enabled;
    }

    bool& UnseenContentMarkerStorage() {
        static bool enabled = true;
        return enabled;
    }

    UnseenContentMarkerStyle& UnseenContentMarkerStyleStorage() {
        static UnseenContentMarkerStyle style = UnseenContentMarkerStyle::Band;
        return style;
    }

} // namespace

void SetUnsavedChangeSwatchEnabled(bool enabled) {
    const std::lock_guard<std::mutex> lock(SettingsMutex());
    UnsavedChangeSwatchStorage() = enabled;
}

bool UnsavedChangeSwatchEnabled() {
    const std::lock_guard<std::mutex> lock(SettingsMutex());
    return UnsavedChangeSwatchStorage();
}

void SetUnseenContentMarkerEnabled(bool enabled) {
    const std::lock_guard<std::mutex> lock(SettingsMutex());
    UnseenContentMarkerStorage() = enabled;
}

bool UnseenContentMarkerEnabled() {
    const std::lock_guard<std::mutex> lock(SettingsMutex());
    return UnseenContentMarkerStorage();
}

void SetUnseenContentMarkerStyle(UnseenContentMarkerStyle style) {
    const std::lock_guard<std::mutex> lock(SettingsMutex());
    UnseenContentMarkerStyleStorage() = style;
}

UnseenContentMarkerStyle GetUnseenContentMarkerStyle() {
    const std::lock_guard<std::mutex> lock(SettingsMutex());
    return UnseenContentMarkerStyleStorage();
}

} // namespace ned::editor
