#include "BufferSave.h"

#include <mutex>

#include "Backup.h"
#include "FinalNewline.h"
#include "LineEndingPolicy.h"
#include "Text/BufferList.h"
#include "TrimOnSave.h"

namespace ned::editor {

namespace {

    std::mutex& DispatcherMutex() {
        static std::mutex mutex;
        return mutex;
    }

    std::function<bool(AsyncSaveRequest)>& DispatcherStorage() {
        static std::function<bool(AsyncSaveRequest)> dispatcher;
        return dispatcher;
    }

    std::function<bool(AsyncSaveRequest)> CurrentDispatcher() {
        const std::scoped_lock lock(DispatcherMutex());
        return DispatcherStorage();
    }

} // namespace

void SetAsyncSaveDispatcher(std::function<bool(AsyncSaveRequest)> dispatcher) {
    const std::scoped_lock lock(DispatcherMutex());
    DispatcherStorage() = std::move(dispatcher);
}

bool ShouldSaveAsynchronously(const text::Buffer& buffer) {
    return buffer.Content().ByteLength() >= text::AsyncLoadThreshold();
}

void RunSavePlanWithBackup(const text::SavePlan& plan, const std::filesystem::path& bufferPath) {
    // backup-and-recovery follow-up: preserve the file's prior on-disk
    // content before the save's rename clobbers it, and drop the
    // now-obsolete crash-recovery autosave once the save has actually
    // succeeded. Both swallow their own failures -- hooked here rather
    // than inside Buffer::Save so Text/ stays policy-free and scratch
    // auto-save (which calls Buffer::Save directly) never creates backup
    // versions.
    BackupFileBeforeSave(bufferPath);
    ExecuteSavePlan(plan);
    RemoveAutoSave(bufferPath);
}

void WriteBufferToDisk(text::Buffer& buffer, SaveDispatch dispatch) {
    if (!buffer.Path()) {
        throw std::runtime_error("ned: buffer \"" + buffer.Name() + "\" has no associated file path");
    }
    const std::filesystem::path bufferPath = *buffer.Path();

    // binary-safety-guardrails follow-up: a buffer opened via a confirmed
    // "open anyway?" binary override gets none of the byte-level,
    // content-changing save-time behaviors below by default -- see
    // BinarySafeguardsActive()'s own doc comment.
    const bool binarySafeguards = buffer.BinarySafeguardsActive();

    const bool                            finalNewline = EnsureFinalNewline() && !binarySafeguards;
    const bool                            trim         = TrimTrailingWhitespaceOnSave() && !binarySafeguards;
    const std::optional<text::LineEnding> ending =
        binarySafeguards ? std::optional<text::LineEnding>{}
                         : std::optional<text::LineEnding>(ResolveLineEndingForSave(buffer.LineEndingKind()));

    text::SavePlan plan = buffer.BeginSave(bufferPath, finalNewline, trim, ending);

    if (dispatch == SaveDispatch::Automatic && ShouldSaveAsynchronously(buffer)) {
        if (const std::function<bool(AsyncSaveRequest)> dispatcher = CurrentDispatcher()) {
            auto progress        = std::make_shared<text::SaveProgress>();
            progress->totalBytes = buffer.Content().ByteLength();
            buffer.SetSaveProgress(progress);

            // Reported from the writing thread; the buffer only ever reads
            // it, so the atomic is the whole of the synchronization.
            plan.onProgress = [progress](std::uintmax_t bytesWritten) {
                progress->bytesWritten.store(bytesWritten, std::memory_order_relaxed);
            };

            AsyncSaveRequest request{buffer.Name(), bufferPath, std::move(plan), std::move(progress)};
            if (dispatcher(std::move(request))) {
                return; // in flight -- the dispatcher owns FinishSave/AbandonSave from here
            }
            // Refused: write it here after all. The plan moved into the
            // request, so the buffer has to be re-armed for a fresh one.
            buffer.AbandonSave();
            plan = buffer.BeginSave(bufferPath, finalNewline, trim, ending);
        }
    }

    try {
        RunSavePlanWithBackup(plan, bufferPath);
    }
    catch (...) {
        buffer.AbandonSave();
        throw;
    }
    buffer.FinishSave(bufferPath, std::move(plan));
}

} // namespace ned::editor
