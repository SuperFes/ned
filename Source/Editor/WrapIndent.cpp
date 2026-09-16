#include "WrapIndent.h"

#include <mutex>

namespace ned::editor {

namespace {

    std::mutex& WrapIndentMutex() {
        static std::mutex mutex;
        return mutex;
    }

    bool& WrapIndentStorage() {
        static bool enabled = true;
        return enabled;
    }

} // namespace

void SetWrapIndent(bool enabled) {
    const std::lock_guard<std::mutex> lock(WrapIndentMutex());
    WrapIndentStorage() = enabled;
}

bool WrapIndent() {
    const std::lock_guard<std::mutex> lock(WrapIndentMutex());
    return WrapIndentStorage();
}

} // namespace ned::editor
