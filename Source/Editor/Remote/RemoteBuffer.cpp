#include "RemoteBuffer.h"

#include "Application.h"
#include "Editor/FileSettings.h"
#include "Editor/ModeOverrides.h"
#include "Editor/PersistentUndo.h"
#include "Editor/Session.h"
#include "Editor/TabWidth.h"
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
    RestoreFilePlace(placeholder, static_cast<std::size_t>(TabWidth()));
    return std::nullopt;
}

namespace {

    // Connects to buffer's host and reads its file, then hands read to the
    // still-open buffer; a failed connection reads as a failed read.
    void ReadRemoteBuffer(Hosts& hosts, text::BufferList& buffers, const text::Buffer& buffer, LifetimeToken token,
                          std::function<void(text::Buffer& buffer, const FsResult<text::FileContents>& read)> done) {
        const text::RemoteFile     file = *buffer.Remote();
        const text::BufferIdentity identity(buffer);
        hosts.Connect(file.destination, token.Bind([&buffers, file, identity, token, done = std::move(done)](
                                                       const std::expected<Connection*, std::string>& connection) {
            text::Buffer* open = buffers.Find(identity);
            if (open == nullptr) {
                return;
            }
            if (!connection) {
                done(*open, std::unexpected(FsFailure{.code = host::fs_error::kFailed, .message = connection.error()}));
                return;
            }
            ReadFile(**connection, file.path, MaxRemoteFileBytes(),
                     token.Bind([&buffers, identity, done](const FsResult<text::FileContents>& read) {
                         if (text::Buffer* reading = buffers.Find(identity)) {
                             done(*reading, read);
                         }
                     }));
        }));
    }

} // namespace

void LoadRemoteBuffer(Hosts& hosts, text::BufferList& buffers, const text::Buffer& placeholder, bool createMissing,
                      LifetimeToken token, std::function<void(text::Buffer& buffer, RemoteLoadOutcome outcome)> done) {
    ReadRemoteBuffer(hosts, buffers, placeholder, std::move(token),
                     [createMissing, done = std::move(done)](text::Buffer& loading, const FsResult<text::FileContents>& read) {
                         if (!read && read.error().code == host::fs_error::kNotFound && !createMissing) {
                             done(loading, {.loaded = std::unexpected(std::string("no such file")), .missing = true});
                             return;
                         }
                         if (const std::optional<std::string> failure = FinishRemoteLoad(loading, read)) {
                             done(loading, {.loaded = std::unexpected(*failure)});
                             return;
                         }
                         done(loading, {.loaded = read ? RemoteLoaded::Read : RemoteLoaded::Created});
                     });
}

void ReloadRemoteBuffer(Hosts& hosts, text::BufferList& buffers, const text::Buffer& buffer, std::optional<text::Charset> charset,
                        LifetimeToken token, std::function<void(text::Buffer& buffer, std::optional<std::string> failure)> done) {
    ReadRemoteBuffer(hosts, buffers, buffer, std::move(token),
                     [charset, done = std::move(done)](text::Buffer& reloading, const FsResult<text::FileContents>& read) {
                         if (!read) {
                             done(reloading, read.error().code == host::fs_error::kNotFound ? "no such file" : read.error().message);
                             return;
                         }
                         try {
                             reloading.RevertFromRemote(*read, charset);
                         }
                         catch (const std::exception& e) {
                             done(reloading, e.what());
                             return;
                         }
                         done(reloading, std::nullopt);
                     });
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
    if (!shared->encodeLater) {
        // Posted, so even a host that answers at once never settles the
        // buffer inside the save that started it.
        postQueue_.Post(lifetime_.Bind([this, shared] { Write(shared); }));
        return true;
    }
    std::erase_if(encoders_, [](const Encoder& encoder) { return encoder.finished->load(); });
    Encoder& encoder = encoders_.emplace_back();
    encoder.thread   = std::jthread([this, shared, finished = encoder.finished, alive = lifetime_.Token()] {
        std::optional<std::string> failure;
        std::string                bytes;
        try {
            bytes = text::EncodeSavePlan(shared->plan);
        }
        catch (const std::exception& e) {
            failure = e.what();
        }
        postQueue_.Post(alive.Bind([this, shared, failure = std::move(failure), bytes = std::move(bytes)]() mutable {
            if (failure) {
                Finish(shared, std::unexpected(FsFailure{.code = host::fs_error::kFailed, .message = *failure}));
                return;
            }
            shared->bytes = std::move(bytes);
            Write(shared);
        }));
        finished->store(true);
    });
    return true;
}

void RemoteSaver::Write(const std::shared_ptr<RemoteSaveRequest>& request) {
    hosts_.Connect(request->file.destination, lifetime_.Bind([this, request](const std::expected<Connection*, std::string>& connection) {
        if (!connection) {
            Finish(request, std::unexpected(FsFailure{.code = host::fs_error::kFailed, .message = connection.error()}));
            return;
        }
        host::FsWriteRequest write{.path = request->file.path, .data = std::move(request->bytes), .expect = request->expect};
        (void)WriteFile(**connection, std::move(write), lifetime_.Bind([this, request](const FsResult<host::FileStat>& written) { Finish(request, written); }));
    }));
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
