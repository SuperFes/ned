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

namespace {

    std::vector<std::pair<std::string, std::string>>& NamedPaintStorage() {
        static std::vector<std::pair<std::string, std::string>> paints;
        return paints;
    }

    std::vector<SurfacePaintOverride>& SurfacePaintStorage() {
        static std::vector<SurfacePaintOverride> surfaces;
        return surfaces;
    }

} // namespace

void AddNamedPaint(const std::string& name, const std::string& spec) {
    const std::lock_guard<std::mutex> lock(NameMutex());
    NamedPaintStorage().emplace_back(name, spec);
}

std::vector<std::pair<std::string, std::string>> NamedPaintOverrides() {
    const std::lock_guard<std::mutex> lock(NameMutex());
    return NamedPaintStorage();
}

void ClearNamedPaintOverrides() {
    const std::lock_guard<std::mutex> lock(NameMutex());
    NamedPaintStorage().clear();
}

void AddSurfacePaint(const std::string& surface, const std::string& part, const std::string& spec) {
    const std::lock_guard<std::mutex> lock(NameMutex());
    SurfacePaintStorage().push_back(SurfacePaintOverride{surface, part, spec});
}

std::vector<SurfacePaintOverride> SurfacePaintOverrides() {
    const std::lock_guard<std::mutex> lock(NameMutex());
    return SurfacePaintStorage();
}

void ClearSurfacePaintOverrides() {
    const std::lock_guard<std::mutex> lock(NameMutex());
    SurfacePaintStorage().clear();
}

} // namespace ned::editor
