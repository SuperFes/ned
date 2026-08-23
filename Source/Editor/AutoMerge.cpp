#include "AutoMerge.h"

#include <exception>
#include <mutex>

namespace ned::editor {

namespace {

    std::mutex& AutoMergeMutex() {
        static std::mutex mutex;
        return mutex;
    }

    bool& AutoMergeStorage() {
        static bool enabled = true;
        return enabled;
    }

} // namespace

void SetAutoMergeEnabled(bool enabled) {
    const std::lock_guard lock(AutoMergeMutex());
    AutoMergeStorage() = enabled;
}

bool AutoMergeEnabled() {
    const std::lock_guard lock(AutoMergeMutex());
    return AutoMergeStorage();
}

std::vector<AutoMergeResult> AutoMergeBuffers(text::BufferList& bufferList) {
    std::vector<AutoMergeResult> merged;
    if (!AutoMergeEnabled()) {
        return merged;
    }
    for (const auto& buffer : bufferList.Buffers()) {
        if (!buffer->Path().has_value() || buffer->IsLoading() || !buffer->Modified() || !buffer->ExternallyModified()) {
            continue;
        }
        try {
            const std::size_t conflictCount = buffer->MergeExternalChanges();
            merged.push_back(AutoMergeResult{buffer->Name(), conflictCount});
        }
        catch (const std::exception&) {
            // Unattended timer context -- see the header comment.
        }
    }
    return merged;
}

} // namespace ned::editor
