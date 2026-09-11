#include "RenameReviewSettings.h"

#include <mutex>

namespace ned::editor {

namespace {

    std::mutex& SettingsMutex() {
        static std::mutex mutex;
        return mutex;
    }

    bool& RenameThroughReviewStorage() {
        static bool enabled = true;
        return enabled;
    }

} // namespace

void SetRenameThroughReview(bool enabled) {
    const std::lock_guard<std::mutex> lock(SettingsMutex());
    RenameThroughReviewStorage() = enabled;
}

bool RenameThroughReview() {
    const std::lock_guard<std::mutex> lock(SettingsMutex());
    return RenameThroughReviewStorage();
}

} // namespace ned::editor
