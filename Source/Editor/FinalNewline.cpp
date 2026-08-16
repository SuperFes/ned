#include "FinalNewline.h"

#include <mutex>

namespace ned::editor {

namespace {

std::mutex& FinalNewlineMutex() {
    static std::mutex mutex;
    return mutex;
}

bool& FinalNewlineStorage() {
    static bool enabled = true;
    return enabled;
}

} // namespace

void SetEnsureFinalNewline(bool enabled) {
    const std::lock_guard<std::mutex> lock(FinalNewlineMutex());
    FinalNewlineStorage() = enabled;
}

bool EnsureFinalNewline() {
    const std::lock_guard<std::mutex> lock(FinalNewlineMutex());
    return FinalNewlineStorage();
}

} // namespace ned::editor
