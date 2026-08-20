#include "AutoRevert.h"

#include <exception>
#include <mutex>

namespace ned::editor {

namespace {

    std::mutex& AutoRevertMutex() {
        static std::mutex mutex;
        return mutex;
    }

    bool& AutoRevertStorage() {
        static bool enabled = true;
        return enabled;
    }

} // namespace

void SetAutoRevertEnabled(bool enabled) {
    const std::lock_guard lock(AutoRevertMutex());
    AutoRevertStorage() = enabled;
}

bool AutoRevertEnabled() {
    const std::lock_guard lock(AutoRevertMutex());
    return AutoRevertStorage();
}

std::vector<std::string> AutoRevertBuffers(text::BufferList& bufferList) {
    std::vector<std::string> reverted;
    if (!AutoRevertEnabled()) {
        return reverted;
    }
    for (const auto& buffer : bufferList.Buffers()) {
        if (!buffer->Path().has_value() || buffer->IsLoading() || buffer->Modified() || !buffer->ExternallyModified()) {
            continue;
        }
        try {
            buffer->Revert();
            reverted.push_back(buffer->Name());
        }
        catch (const std::exception&) {
            // Unattended timer context -- see the header comment.
        }
    }
    return reverted;
}

} // namespace ned::editor
