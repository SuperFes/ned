#include "IndentStyle.h"

#include <algorithm>
#include <mutex>
#include <unordered_map>

namespace ned::editor {

namespace {

    std::mutex& StyleMutex() {
        static std::mutex mutex;
        return mutex;
    }

    IndentStyle& DefaultStorage() {
        static IndentStyle style;
        return style;
    }

    std::unordered_map<std::string, IndentStyle>& PerModeStorage() {
        static std::unordered_map<std::string, IndentStyle> perMode;
        return perMode;
    }

    IndentStyle Clamped(IndentStyle style) {
        style.width = std::max(1, style.width); // non-positive would hang/underflow IndentString's expansion loop
        return style;
    }

} // namespace

void SetIndentStyle(IndentStyle style) {
    const std::lock_guard<std::mutex> lock(StyleMutex());
    DefaultStorage() = Clamped(style);
}

IndentStyle DefaultIndentStyle() {
    const std::lock_guard<std::mutex> lock(StyleMutex());
    return DefaultStorage();
}

void SetIndentStyleForMode(const std::string& modeName, IndentStyle style) {
    const std::lock_guard<std::mutex> lock(StyleMutex());
    PerModeStorage().insert_or_assign(modeName, Clamped(style));
}

IndentStyle EffectiveIndentStyle(const std::string& modeName) {
    const std::lock_guard<std::mutex> lock(StyleMutex());
    if (const auto it = PerModeStorage().find(modeName); it != PerModeStorage().end()) {
        return it->second;
    }
    return DefaultStorage();
}

} // namespace ned::editor
