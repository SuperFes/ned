#include "BufferSave.h"

#include <mutex>
#include <vector>

#include "Application.h"
#include "Backup.h"
#include "FinalNewline.h"
#include "LineEndingPolicy.h"
#include "Mode.h"
#include "ModeOverrides.h"
#include "Remote/Fs.h"
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

    std::function<bool(RemoteSaveRequest)>& RemoteDispatcherStorage() {
        static std::function<bool(RemoteSaveRequest)> dispatcher;
        return dispatcher;
    }

    std::function<bool(RemoteSaveRequest)> CurrentRemoteDispatcher() {
        const std::scoped_lock lock(DispatcherMutex());
        return RemoteDispatcherStorage();
    }

    void WriteRemoteBuffer(text::Buffer& buffer, SaveDispatch dispatch, bool overwriteChanged, text::SavePlan plan) {
        const text::RemoteFile& file = *buffer.Remote();
        const auto              fail = [&buffer](const std::string& message) {
            buffer.AbandonSave();
            throw std::runtime_error(message);
        };
        // Its outcome can only arrive later, which no synchronous caller
        // could report.
        if (dispatch == SaveDispatch::ForceSynchronous) {
            fail("ned: " + file.ToString() + " is on a remote host; save-buffer saves it");
        }
        const std::function<bool(RemoteSaveRequest)> dispatcher = CurrentRemoteDispatcher();
        if (!dispatcher) {
            fail("ned: no remote hosts to save " + file.ToString() + " through");
        }
        RemoteSaveRequest request{.buffer = text::BufferIdentity(buffer), .file = file, .bytes = {}, .expect = std::nullopt, .plan = {}};
        try {
            request.bytes = text::EncodeSavePlan(plan);
        }
        catch (const std::exception& e) {
            fail(e.what());
        }
        if (request.bytes.size() > remote::kMaxWriteBytes) {
            fail("ned: " + file.ToString() + " is too large to save to its host (" + std::to_string(request.bytes.size() >> 20) + " MiB, at most " +
                 std::to_string(remote::kMaxWriteBytes >> 20) + ")");
        }
        if (!overwriteChanged) {
            request.expect = buffer.RemoteTimestamp();
        }
        request.plan = std::move(plan);
        if (!dispatcher(std::move(request))) {
            fail("ned: couldn't start saving " + file.ToString());
        }
    }

} // namespace

void SetAsyncSaveDispatcher(std::function<bool(AsyncSaveRequest)> dispatcher) {
    const std::scoped_lock lock(DispatcherMutex());
    DispatcherStorage() = std::move(dispatcher);
}

void SetRemoteSaveDispatcher(std::function<bool(RemoteSaveRequest)> dispatcher) {
    const std::scoped_lock lock(DispatcherMutex());
    RemoteDispatcherStorage() = std::move(dispatcher);
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
    text::ValidateSavePlanCharset(plan);
    BackupFileBeforeSave(bufferPath);
    ExecuteSavePlan(plan);
    RemoveAutoSave(bufferPath);
}

text::Charset CharsetForSave(const text::Buffer& buffer) {
    const std::optional<text::Charset> stated = buffer.Conventions().charset;
    if (!stated || buffer.CharsetChosen() || buffer.BinarySafeguardsActive() ||
        (buffer.Content().IsHuge() && !text::IsUtf8Family(*stated))) {
        return buffer.FileCharset();
    }
    return *stated;
}

void WriteBufferToDisk(text::Buffer& buffer, SaveDispatch dispatch, bool overwriteChanged) {
    if (!buffer.Path() && !buffer.Remote()) {
        throw std::runtime_error("ned: buffer \"" + buffer.Name() + "\" has no associated file path");
    }

    // binary-safety-guardrails follow-up: a buffer opened via a confirmed
    // "open anyway?" binary override gets none of the byte-level,
    // content-changing save-time behaviors below by default -- see
    // BinarySafeguardsActive()'s own doc comment.
    const bool binarySafeguards = buffer.BinarySafeguardsActive();

    const bool                            finalNewline = EnsureFinalNewline(buffer) && !binarySafeguards;
    bool                                  trim         = TrimTrailingWhitespaceOnSave(buffer) && !binarySafeguards;
    // Trailing whitespace the mode says is content stays. The huge-file
    // writer streams without a parse to ask, so such a mode's huge buffer
    // is not trimmed at all.
    std::vector<std::size_t> keepTrailingWhitespace;
    if (trim) {
        if (const Mode mode = CachedModeForBuffer(buffer); mode.keptTrailingWhitespace) {
            if (buffer.Content().IsHuge()) {
                trim = false;
            }
            else {
                keepTrailingWhitespace = mode.keptTrailingWhitespace(buffer.Text());
            }
        }
    }
    const std::optional<text::LineEnding> ending =
        binarySafeguards ? std::optional<text::LineEnding>{} : std::optional<text::LineEnding>(ResolveLineEndingForSave(buffer));

    const text::Charset charset = CharsetForSave(buffer);
    if (buffer.Remote()) {
        text::SavePlan plan         = buffer.BeginRemoteSave(finalNewline, trim, ending);
        plan.charset                = charset;
        plan.keepTrailingWhitespace = keepTrailingWhitespace;
        WriteRemoteBuffer(buffer, dispatch, overwriteChanged, std::move(plan));
        return;
    }

    const std::filesystem::path bufferPath = *buffer.Path();
    const auto                  beginPlan  = [&] {
        text::SavePlan plan         = buffer.BeginSave(bufferPath, finalNewline, trim, ending);
        plan.charset                = charset;
        plan.keepTrailingWhitespace = keepTrailingWhitespace;
        return plan;
    };
    text::SavePlan plan = beginPlan();

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
            plan = beginPlan();
        }
    }

    try {
        RunSavePlanWithBackup(plan, bufferPath);
    }
    catch (const std::exception& e) {
        buffer.AbandonSave();
        // Recorded here as well as on the asynchronous path: a save the
        // user watched fail and then gave up on is still a file that is not
        // there. The caller shows the message immediately too, but that
        // leaves with the editor, which is no help to whoever reads the
        // exit code afterwards.
        Ned::Application::NoteSaveFailed(bufferPath, buffer.Name(), e.what());
        throw;
    }
    catch (...) {
        buffer.AbandonSave();
        Ned::Application::NoteSaveFailed(bufferPath, buffer.Name(), std::string());
        throw;
    }
    buffer.FinishSave(bufferPath, std::move(plan));
    Ned::Application::NoteSaveSucceeded(bufferPath);
}

} // namespace ned::editor
