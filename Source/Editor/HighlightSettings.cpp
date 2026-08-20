#include "HighlightSettings.h"

#include <mutex>

namespace ned::editor {

namespace {

    constexpr std::size_t kDefaultMaxHighlightBytes = 8 * 1024 * 1024;

    std::mutex& HighlightMutex() {
        static std::mutex mutex;
        return mutex;
    }

    std::size_t& MaxHighlightBytesStorage() {
        static std::size_t bytes = kDefaultMaxHighlightBytes;
        return bytes;
    }

} // namespace

void SetMaxHighlightBytes(std::size_t bytes) {
    const std::lock_guard<std::mutex> lock(HighlightMutex());
    MaxHighlightBytesStorage() = bytes;
}

std::size_t MaxHighlightBytes() {
    const std::lock_guard<std::mutex> lock(HighlightMutex());
    return MaxHighlightBytesStorage();
}

} // namespace ned::editor
