#include "TrimOnSave.h"

#include <mutex>

#include "Text/Buffer.h"

namespace ned::editor {

namespace {

    std::mutex& TrimOnSaveMutex() {
        static std::mutex mutex;
        return mutex;
    }

    bool& TrimOnSaveStorage() {
        static bool enabled = true;
        return enabled;
    }

} // namespace

void SetTrimTrailingWhitespaceOnSave(bool enabled) {
    const std::lock_guard<std::mutex> lock(TrimOnSaveMutex());
    TrimOnSaveStorage() = enabled;
}

bool TrimTrailingWhitespaceOnSave() {
    const std::lock_guard<std::mutex> lock(TrimOnSaveMutex());
    return TrimOnSaveStorage();
}

bool TrimTrailingWhitespaceOnSave(const text::Buffer& buffer) {
    return buffer.Conventions().trimTrailingWhitespace.value_or(TrimTrailingWhitespaceOnSave());
}

} // namespace ned::editor
