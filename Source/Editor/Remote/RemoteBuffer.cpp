#include "RemoteBuffer.h"

#include "Application.h"
#include "Editor/FileSettings.h"
#include "Editor/ModeOverrides.h"
#include "Editor/PersistentUndo.h"
#include "Text/BinaryDetect.h"
#include "Text/BufferList.h"

namespace ned::editor::remote {

std::size_t MaxRemoteFileBytes() {
    return static_cast<std::size_t>(text::HugeFileThreshold());
}

std::optional<std::string> FinishRemoteLoad(text::Buffer& placeholder, const FsResult<text::FileContents>& read) {
    const text::RemoteFile& file = *placeholder.Remote();
    if (!read) {
        if (read.error().code != host::fs_error::kNotFound) {
            return read.error().message;
        }
        placeholder.FinishLoad(text::Rope());
        placeholder.SetRemoteTimestamp(std::nullopt);
    }
    else {
        try {
            const text::Buffer decoded = text::Buffer::FromRemoteContents(file, *read);
            placeholder.FinishLoad(text::Rope(decoded.Text()), decoded.LineEndingKind(), decoded.FileCharset());
        }
        catch (const text::BinaryFileError& e) {
            return e.what();
        }
        placeholder.SetRemoteTimestamp(read->timestamp);
    }
    // The placeholder's mode was settled from its path alone.
    ClearModeCacheFor(placeholder);
    ApplyFileSettings(placeholder);
    TryRestoreUndoHistory(placeholder);
    return std::nullopt;
}

void LoadRemoteBuffer(Hosts& hosts, text::BufferList& buffers, const text::Buffer& placeholder, bool createMissing,
                      LifetimeToken token, std::function<void(text::Buffer& buffer, RemoteLoadOutcome outcome)> done) {
    const text::RemoteFile     file = *placeholder.Remote();
    const text::BufferIdentity identity(placeholder);
    hosts.Connect(file.destination, token.Bind([&buffers, file, identity, createMissing, token, done = std::move(done)](
                                                   const std::expected<Connection*, std::string>& connection) {
        text::Buffer* buffer = buffers.Find(identity);
        if (buffer == nullptr) {
            return;
        }
        if (!connection) {
            done(*buffer, {.loaded = std::unexpected(connection.error())});
            return;
        }
        ReadFile(**connection, file.path, MaxRemoteFileBytes(),
                 token.Bind([&buffers, identity, createMissing, done](const FsResult<text::FileContents>& read) {
                     text::Buffer* loading = buffers.Find(identity);
                     if (loading == nullptr) {
                         return;
                     }
                     const bool missing = !read && read.error().code == host::fs_error::kNotFound;
                     if (missing && !createMissing) {
                         done(*loading, {.loaded = std::unexpected(std::string("no such file")), .missing = true});
                         return;
                     }
                     if (const std::optional<std::string> failure = FinishRemoteLoad(*loading, read)) {
                         done(*loading, {.loaded = std::unexpected(*failure)});
                         return;
                     }
                     done(*loading, {.loaded = read ? RemoteLoaded::Read : RemoteLoaded::Created});
                 }));
    }));
}

std::optional<std::string> FinishRemoteSave(text::Buffer& buffer, text::SavePlan plan, const FsResult<host::FileStat>& written) {
    if (written) {
        buffer.FinishRemoteSave(std::move(plan), written->timestamp);
        return std::nullopt;
    }
    buffer.AbandonSave();
    if (written.error().code == host::fs_error::kFileChanged) {
        buffer.NoteRemoteChanged();
        return written.error().message + "; saving again asks to overwrite it";
    }
    return written.error().message;
}

RemoteSaver::RemoteSaver(Hosts& hosts, text::BufferList& buffers, PostQueue& postQueue, std::function<void(std::string message)> report) : hosts_(hosts), buffers_(buffers), postQueue_(postQueue), report_(std::move(report)) {
}

bool RemoteSaver::Dispatch(RemoteSaveRequest request) {
    auto shared = std::make_shared<RemoteSaveRequest>(std::move(request));
    // Posted, so even a host that answers at once never settles the buffer
    // inside the save that started it.
    postQueue_.Post(lifetime_.Bind([this, shared] {
        hosts_.Connect(shared->file.destination, lifetime_.Bind([this, shared](const std::expected<Connection*, std::string>& connection) {
            if (!connection) {
                Finish(shared, std::unexpected(FsFailure{.code = host::fs_error::kFailed, .message = connection.error()}));
                return;
            }
            host::FsWriteRequest write{.path = shared->file.path, .data = std::move(shared->bytes), .expect = shared->expect};
            (void)WriteFile(**connection, std::move(write),
                            lifetime_.Bind([this, shared](const FsResult<host::FileStat>& written) { Finish(shared, written); }));
        }));
    }));
    return true;
}

void RemoteSaver::Finish(const std::shared_ptr<RemoteSaveRequest>& request, const FsResult<host::FileStat>& written) {
    const std::string where = request->file.ToString();
    // A buffer closed meanwhile has nothing left to settle; the file is
    // written or not regardless.
    text::Buffer*                    buffer  = buffers_.Find(request->buffer);
    const std::optional<std::string> failure = buffer != nullptr ? FinishRemoteSave(*buffer, std::move(request->plan), written)
                                               : written         ? std::nullopt
                                                                 : std::optional<std::string>(written.error().message);
    if (failure) {
        Ned::Application::NoteSaveFailed(where, buffer != nullptr ? buffer->Name() : std::string(), *failure);
        report_("Couldn't save " + where + ": " + *failure);
        return;
    }
    Ned::Application::NoteSaveSucceeded(where);
    report_("Wrote " + where);
}

} // namespace ned::editor::remote
