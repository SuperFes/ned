#include "DiffRefreshSettings.h"

#include <algorithm>
#include <mutex>

namespace ned::editor {

namespace {

    std::mutex& DebounceMutex() {
        static std::mutex mutex;
        return mutex;
    }

    int& DebounceMsStorage() {
        static int milliseconds = 1200;
        return milliseconds;
    }

} // namespace

void SetDiffRefreshDebounceMs(int milliseconds) {
    const std::lock_guard<std::mutex> lock(DebounceMutex());
    DebounceMsStorage() = std::max(1, milliseconds);
}

std::chrono::milliseconds DiffRefreshDebounce() {
    const std::lock_guard<std::mutex> lock(DebounceMutex());
    return std::chrono::milliseconds(DebounceMsStorage());
}

} // namespace ned::editor
