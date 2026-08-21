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

} // namespace ned::editor
