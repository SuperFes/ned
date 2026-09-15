#include "MaxConsecutiveBlankLines.h"

#include <mutex>

namespace ned::editor {

namespace {

    std::mutex& Mutex() {
        static std::mutex mutex;
        return mutex;
    }

    std::optional<int>& Storage() {
        static std::optional<int> value = 2; // default: a generous, low-risk cap
        return value;
    }

} // namespace

void SetMaxConsecutiveBlankLines(std::optional<int> max) {
    const std::lock_guard<std::mutex> lock(Mutex());
    Storage() = (max && *max < 0) ? std::nullopt : max; // a negative value means the same as unset -- both are "no limit"
}

std::optional<int> MaxConsecutiveBlankLines() {
    const std::lock_guard<std::mutex> lock(Mutex());
    return Storage();
}

} // namespace ned::editor
