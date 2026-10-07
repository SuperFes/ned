#include "ExternalChanges.h"

#include <algorithm>
#include <exception>
#include <filesystem>
#include <memory>
#include <optional>
#include <utility>

#include "Editor/AutoMerge.h"
#include "Editor/AutoRevert.h"
#include "Editor/Remote/Fs.h"
#include "Editor/Remote/RemoteBuffer.h"

namespace ned::editor::remote {

namespace {

    using Timestamp = std::optional<std::filesystem::file_time_type>;
    using Report    = std::shared_ptr<std::function<void(const RemoteChange&)>>;

    // The buffer, if it's still open on the same file with the timestamp
    // the check started from, and nothing is settling it meanwhile.
    text::Buffer* Unchanged(text::BufferList& buffers, const text::BufferIdentity& identity, const text::RemoteFile& file,
                            const Timestamp& seen) {
        text::Buffer* buffer = buffers.Find(identity);
        if (buffer == nullptr || buffer->Remote() != file || buffer->RemoteTimestamp() != seen || buffer->IsSaving() || buffer->IsLoading()) {
            return nullptr;
        }
        return buffer;
    }

    void Settle(text::Buffer& buffer, const text::FileContents& contents, const Report& report) {
        RemoteChange change{.name = buffer.Name(), .outcome = RemoteChange::Outcome::Noted, .conflicts = 0};
        try {
            if (!buffer.Modified() && AutoRevertEnabled()) {
                buffer.RevertFromRemote(contents);
                change.outcome = RemoteChange::Outcome::Reverted;
            }
            else if (buffer.Modified() && AutoMergeEnabled()) {
                change.conflicts = buffer.MergeRemoteChanges(contents);
                change.outcome   = RemoteChange::Outcome::Merged;
            }
            else {
                buffer.NoteRemoteChanged();
            }
        }
        catch (const std::exception&) {
            // Binary now, say: leave the buffer be, but its next save asks.
            buffer.NoteRemoteChanged();
        }
        (*report)(change);
    }

    void Check(Hosts& hosts, Connection& connection, text::BufferList& buffers, const text::Buffer& buffer, const LifetimeToken& token,
               const Report& report) {
        const text::RemoteFile     file = *buffer.Remote();
        const text::BufferIdentity identity(buffer);
        const Timestamp            seen = buffer.RemoteTimestamp();
        StatFile(connection, file.path, token.Bind([&hosts, &buffers, file, identity, seen, token, report](const FsResult<host::FileStat>& stat) {
            if (!stat || stat->kind != host::FileKind::File || stat->timestamp == seen || Unchanged(buffers, identity, file, seen) == nullptr) {
                return;
            }
            Connection* live = hosts.Find(file.destination);
            if (live == nullptr || !live->Connected()) {
                return;
            }
            ReadFile(*live, file.path, MaxRemoteFileBytes(), token.Bind([&buffers, file, identity, seen, report](const FsResult<text::FileContents>& read) {
                text::Buffer* buffer = Unchanged(buffers, identity, file, seen);
                if (buffer == nullptr) {
                    return;
                }
                if (!read) {
                    buffer->NoteRemoteChanged();
                    (*report)(RemoteChange{.name = buffer->Name(), .outcome = RemoteChange::Outcome::Noted, .conflicts = 0});
                    return;
                }
                Settle(*buffer, *read, report);
            }));
        }));
    }

} // namespace

void FollowRemoteChanges(Hosts& hosts, text::BufferList& buffers, const std::string& destination, const host::WatchEvents& events,
                         LifetimeToken token, std::function<void(const RemoteChange& change)> report) {
    Connection* connection = hosts.Find(destination);
    if (connection == nullptr || !connection->Connected()) {
        return;
    }
    const auto named = [&events](const std::filesystem::path& path) {
        return events.overflow || std::ranges::any_of(events.events, [&path](const FileEvent& event) { return event.path == path; });
    };
    const Report shared = std::make_shared<std::function<void(const RemoteChange&)>>(std::move(report));
    for (const auto& buffer : buffers.Buffers()) {
        const std::optional<text::RemoteFile>& file = buffer->Remote();
        if (!file || file->destination != destination || buffer->IsLoading() || !named(file->path)) {
            continue;
        }
        Check(hosts, *connection, buffers, *buffer, token, shared);
    }
}

} // namespace ned::editor::remote
