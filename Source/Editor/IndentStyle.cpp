#include "IndentStyle.h"

#include <algorithm>
#include <mutex>
#include <optional>
#include <unordered_map>

#include "ImprintBracket.h"
#include "IndentDefaults.h"

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
    // configurable-formatter follow-up: the compiled-in per-language safe
    // default (IndentDefaults.h) sits between "no per-mode override" and
    // the flat process-wide default -- languageKey is the inverse of
    // ModeNameFor (LanguageDefinition.h), so this is exact for every
    // bundled or registered language and simply misses for anything else
    // (FundamentalMode, a made-up test mode name), falling through below.
    if (const std::optional<IndentStyle> builtin = BuiltinIndentStyleForLanguage(imprint::LanguageKeyForMode(modeName))) {
        return *builtin;
    }
    return DefaultStorage();
}

} // namespace ned::editor
