#include "Search.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "Editor/Remote/Fs.h"

namespace ned::editor::remote {

std::uint64_t SearchProject(Connection& connection, const host::SearchRequest& request,
                            std::function<void(std::vector<SearchMatch> matches)>   onMatches,
                            std::function<void(std::optional<std::string> failure)> onDone) {
    struct State {
        std::uint64_t              id = 0;
        std::optional<std::string> rejected;
    };
    auto                state    = std::make_shared<State>();
    protocol::Endpoint& endpoint = connection.Endpoint();

    auto finish = [state, onDone](std::optional<std::string> failure) { onDone(state->rejected ? state->rejected : std::move(failure)); };
    state->id   = endpoint.Request(
        std::string(host::kSearchProject), host::ToJson(request),
        protocol::ResponseHandlers{
            [finish](const nlohmann::json&) { finish(std::nullopt); },
            [finish](const protocol::RemoteError& error) { finish(error.message); },
            [state, &endpoint, root = request.root, onMatches = std::move(onMatches)](const nlohmann::json& data) {
                if (state->rejected) {
                    return;
                }
                try {
                    onMatches(host::MatchesFromHost(data, root));
                }
                catch (const protocol::ProtocolError& e) {
                    state->rejected = std::string("the host's search results are malformed: ") + e.what();
                    endpoint.Cancel(state->id);
                }
            }});
    return state->id;
}

void GatherRemoteLive(Connection& connection, const text::BufferList& buffers, const text::RemoteFile& root, LifetimeToken token,
                      std::function<void(std::expected<RemoteLive, std::string> live)> done, std::size_t maxInline) {
    struct Pending {
        std::filesystem::path path;
        std::string           text;
    };
    auto pending = std::make_shared<std::vector<Pending>>();
    for (const auto& buffer : buffers.Buffers()) {
        const std::optional<text::RemoteFile>& file = buffer->Remote();
        if (file && file->destination == root.destination && buffer->Modified() && !buffer->IsLoading()) {
            pending->push_back(Pending{.path = file->path, .text = buffer->Text()});
        }
    }
    if (pending->empty()) {
        if (token.Alive()) {
            done(RemoteLive{});
        }
        return;
    }
    struct State {
        RemoteLive                                                  live;
        std::size_t                                                 outstanding = 0;
        std::size_t                                                 inlineBytes = 0;
        bool                                                        failed      = false;
        std::function<void(std::expected<RemoteLive, std::string>)> done;
    };
    auto       state   = std::make_shared<State>(State{.live = {}, .outstanding = pending->size(), .inlineBytes = 0, .failed = false, .done = std::move(done)});
    const bool uploads = connection.Hello().capabilities.fs >= 6;
    const auto under   = [](const std::filesystem::path& path, const std::filesystem::path& directory) {
        const std::filesystem::path relative = path.lexically_relative(directory);
        return !relative.empty() && *relative.begin() != "..";
    };
    // The root's real path first, so a buffer opened through a symlink still
    // counts as under it.
    (void)ResolvePath(
        connection, root.path,
        token.Bind([&connection, token, pending, state, uploads, maxInline, under, root = root.path](FsResult<std::filesystem::path> realRoot) {
            const std::filesystem::path resolvedRoot = realRoot ? *realRoot : root;
            const auto                  settle       = [state] {
                if (--state->outstanding == 0 && !state->failed) {
                    state->done(std::move(state->live));
                }
            };
            for (Pending& entry : *pending) {
                const std::filesystem::path path = entry.path;
                (void)ResolvePath(
                    connection, path,
                    token.Bind([&connection, state, uploads, maxInline, under, root, resolvedRoot, settle, path, text = std::move(entry.text)](
                                   FsResult<std::filesystem::path> real) mutable {
                        if (state->failed) {
                            return;
                        }
                        // A new file, not on the host yet, goes by its own name.
                        const std::filesystem::path canonical = real ? *real : path;
                        if (!under(path, root) && !under(canonical, resolvedRoot)) {
                            settle();
                            return;
                        }
                        if (state->inlineBytes + text.size() <= maxInline || !uploads) {
                            if (state->inlineBytes + text.size() <= maxInline) {
                                state->inlineBytes += text.size();
                                state->live.inlined.push_back(LiveContent{path, canonical, std::move(text), std::nullopt});
                            }
                            settle();
                            return;
                        }
                        const std::uint64_t handle = NewUploadHandle();
                        Upload(connection, handle, std::make_shared<const std::string>(std::move(text)),
                               [state, settle, path, canonical, handle](std::optional<FsFailure> failure) {
                                   if (failure) {
                                       if (!std::exchange(state->failed, true)) {
                                           state->done(std::unexpected("sending " + path.string() + " failed: " + failure->message));
                                       }
                                       return;
                                   }
                                   state->live.uploaded.push_back(host::LiveUpload{.path = path, .canonicalPath = canonical, .upload = handle});
                                   settle();
                               });
                    }));
            }
        }));
}

host::SearchRequest SearchRequestWithLive(const text::RemoteFile& root, std::string pattern, RemoteLive live) {
    return host::SearchRequest{.root = root.path, .pattern = std::move(pattern), .live = std::move(live.inlined), .liveUploads = std::move(live.uploaded)};
}

} // namespace ned::editor::remote
