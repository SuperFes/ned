#include "ColorSwatchSettings.h"

#include <mutex>

namespace ned::editor {

namespace {

    std::mutex& SettingsMutex() {
        static std::mutex mutex;
        return mutex;
    }

    bool& ColorSwatchesStorage() {
        static bool enabled = true;
        return enabled;
    }

    ColorSwatchStyle& ColorSwatchStyleStorage() {
        static ColorSwatchStyle style = ColorSwatchStyle::Block;
        return style;
    }

} // namespace

void SetColorSwatchesEnabled(bool enabled) {
    const std::lock_guard<std::mutex> lock(SettingsMutex());
    ColorSwatchesStorage() = enabled;
}

bool ColorSwatchesEnabled() {
    const std::lock_guard<std::mutex> lock(SettingsMutex());
    return ColorSwatchesStorage();
}

void SetColorSwatchStyle(ColorSwatchStyle style) {
    const std::lock_guard<std::mutex> lock(SettingsMutex());
    ColorSwatchStyleStorage() = style;
}

ColorSwatchStyle GetColorSwatchStyle() {
    const std::lock_guard<std::mutex> lock(SettingsMutex());
    return ColorSwatchStyleStorage();
}

} // namespace ned::editor
