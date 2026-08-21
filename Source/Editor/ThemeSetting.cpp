#include "ThemeSetting.h"

#include <mutex>

namespace ned::editor {

namespace {

    std::mutex& NameMutex() {
        static std::mutex mutex;
        return mutex;
    }

    std::string& NameStorage() {
        static std::string name;
        return name;
    }

} // namespace

void SetPreferredThemeName(const std::string& name) {
    const std::lock_guard<std::mutex> lock(NameMutex());
    NameStorage() = name;
}

std::string PreferredThemeName() {
    const std::lock_guard<std::mutex> lock(NameMutex());
    return NameStorage();
}

namespace {

    std::vector<std::pair<std::string, std::string>>& OverrideStorage() {
        static std::vector<std::pair<std::string, std::string>> overrides;
        return overrides;
    }

} // namespace

void AddThemeColorOverride(const std::string& key, const std::string& token) {
    const std::lock_guard<std::mutex> lock(NameMutex());
    OverrideStorage().emplace_back(key, token);
}

std::vector<std::pair<std::string, std::string>> ThemeColorOverrides() {
    const std::lock_guard<std::mutex> lock(NameMutex());
    return OverrideStorage();
}

void ClearThemeColorOverrides() {
    const std::lock_guard<std::mutex> lock(NameMutex());
    OverrideStorage().clear();
}

} // namespace ned::editor
