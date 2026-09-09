#include "BackgroundSync.h"

#include <mutex>

#include "Editor/Mode.h"
#include "Editor/ModeOverrides.h"
#include "Text/BufferList.h"

#include "Manager.h"

namespace ned::editor::lsp {

namespace {

    std::mutex& BackgroundSyncMutex() {
        static std::mutex mutex;
        return mutex;
    }

    bool& BackgroundSyncStorage() {
        static bool enabled = true;
        return enabled;
    }

} // namespace

void SetLspBackgroundSyncEnabled(bool enabled) {
    const std::lock_guard lock(BackgroundSyncMutex());
    BackgroundSyncStorage() = enabled;
}

bool BackgroundSyncEnabled() {
    const std::lock_guard lock(BackgroundSyncMutex());
    return BackgroundSyncStorage();
}

void SyncBackgroundBuffers(text::BufferList& bufferList, Manager& manager) {
    if (!BackgroundSyncEnabled()) {
        return;
    }
    for (const auto& buffer : bufferList.Buffers()) {
        if (!buffer->Path().has_value() || buffer->IsLoading()) {
            continue;
        }
        const Mode mode = CachedModeForBuffer(*buffer);
        manager.SyncBuffer(*buffer, LanguageKeyForMode(mode));
    }
}

} // namespace ned::editor::lsp
