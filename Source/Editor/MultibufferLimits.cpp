#include "MultibufferLimits.h"

#include <mutex>

namespace ned::editor {

namespace {

    std::mutex& SettingsMutex() {
        static std::mutex mutex;
        return mutex;
    }

    std::size_t& MaxExcerptsStorage() {
        static std::size_t count = 500;
        return count;
    }

} // namespace

void SetMultibufferMaxExcerpts(std::size_t count) {
    const std::lock_guard<std::mutex> lock(SettingsMutex());
    MaxExcerptsStorage() = count;
}

std::size_t MultibufferMaxExcerpts() {
    const std::lock_guard<std::mutex> lock(SettingsMutex());
    return MaxExcerptsStorage();
}

} // namespace ned::editor
