#include "LspBackgroundSync.h"

#include <mutex>

#include "Editor/Mode.h"
#include "Editor/ModeOverrides.h"
#include "Text/BufferList.h"

#include "LspManager.h"

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

bool LspBackgroundSyncEnabled() {
    const std::lock_guard lock(BackgroundSyncMutex());
    return BackgroundSyncStorage();
}

void SyncBackgroundBuffers(text::BufferList& bufferList, LspManager& manager) {
    if (!LspBackgroundSyncEnabled()) {
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
