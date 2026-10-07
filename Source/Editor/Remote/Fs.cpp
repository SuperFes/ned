#include "Fs.h"

#include <algorithm>
#include <iterator>
#include <memory>
#include <optional>
#include <utility>

#include "Editor/Host/Codec.h"
#include "Editor/ProcessTimeouts.h"

namespace ned::editor::remote {

using Json = nlohmann::json;

namespace {

    // Slow enough for a poor mobile link.
    constexpr std::size_t kSlowestUploadBytesPerSecond = 256 * 1024;

    FsFailure FromRemote(const protocol::RemoteError& error) {
        return FsFailure{.code = error.code, .message = error.message};
    }

    FsFailure Malformed(const std::string& what) {
        return FsFailure{.code = host::fs_error::kFailed, .message = "the host's answer is malformed: " + what};
    }

    // A stat answer, decoded; done gets the outcome.
    protocol::ResponseHandlers StatHandlers(std::function<void(FsResult<host::FileStat>)> done) {
        auto shared = std::make_shared<std::function<void(FsResult<host::FileStat>)>>(std::move(done));
        return protocol::ResponseHandlers{[shared](const Json& result) {
                                              try {
                                                  (*shared)(host::FileStatFromJson(result));
                                              }
                                              catch (const protocol::ProtocolError& e) {
                                                  (*shared)(std::unexpected(Malformed(e.what())));
                                              }
                                          },
                                          [shared](const protocol::RemoteError& error) { (*shared)(std::unexpected(FromRemote(error))); },
                                          {}};
    }

    // An answer with nothing in it but success.
    protocol::ResponseHandlers DoneHandlers(std::function<void(FsResult<void>)> done) {
        auto shared = std::make_shared<std::function<void(FsResult<void>)>>(std::move(done));
        return protocol::ResponseHandlers{[shared](const Json&) { (*shared)({}); },
                                          [shared](const protocol::RemoteError& error) { (*shared)(std::unexpected(FromRemote(error))); },
                                          {}};
    }

} // namespace

std::chrono::milliseconds WriteIdleLimit(std::size_t bytes) {
    return ProtocolRequestTimeoutMs() + std::chrono::milliseconds(bytes * 1000 / kSlowestUploadBytesPerSecond);
}

std::uint64_t StatFile(Connection& connection, const std::filesystem::path& path, std::function<void(FsResult<host::FileStat>)> done) {
    return connection.Endpoint().Request(std::string(host::kFsStat), host::PathParams(path), StatHandlers(std::move(done)), ProtocolRequestTimeoutMs());
}

std::uint64_t ReadFile(Connection& connection, const std::filesystem::path& path, std::size_t maxBytes,
                       std::function<void(FsResult<text::FileContents>)> done) {
    struct State {
        std::uint64_t            id = 0;
        std::string              bytes;
        std::optional<FsFailure> rejected;
    };
    auto                state    = std::make_shared<State>();
    auto                finish   = std::make_shared<std::function<void(FsResult<text::FileContents>)>>(std::move(done));
    protocol::Endpoint& endpoint = connection.Endpoint();
    state->id                    = endpoint.Request(
        std::string(host::kFsRead), host::PathParams(path),
        protocol::ResponseHandlers{
            [state, finish](const Json& result) {
                if (state->rejected) {
                    (*finish)(std::unexpected(*state->rejected));
                    return;
                }
                try {
                    const host::FileStat stat = host::FileStatFromJson(result);
                    (*finish)(text::FileContents{.bytes = std::move(state->bytes), .timestamp = stat.timestamp});
                }
                catch (const protocol::ProtocolError& e) {
                    (*finish)(std::unexpected(Malformed(e.what())));
                }
            },
            [state, finish](const protocol::RemoteError& error) { (*finish)(std::unexpected(state->rejected ? *state->rejected : FromRemote(error))); },
            [state, &endpoint, maxBytes, path](const Json& data) {
                if (state->rejected) {
                    return;
                }
                if (!data.is_binary()) {
                    state->rejected = Malformed("a chunk of " + path.string() + " isn't bytes");
                }
                else if (state->bytes.size() + data.get_binary().size() > maxBytes) {
                    state->rejected = FsFailure{.code    = host::fs_error::kFailed,
                                                .message = path.string() + " is larger than " + std::to_string(maxBytes) + " bytes"};
                }
                else {
                    state->bytes.append(data.get_binary().begin(), data.get_binary().end());
                    return;
                }
                state->bytes.clear();
                endpoint.Cancel(state->id);
            }},
        ProtocolRequestTimeoutMs());
    return state->id;
}

std::uint64_t WriteFile(Connection& connection, host::FsWriteRequest request, std::function<void(FsResult<host::FileStat>)> done) {
    if (request.data.size() > kMaxWriteBytes) {
        done(std::unexpected(FsFailure{.code    = host::fs_error::kFailed,
                                       .message = request.path.string() + " is too large to write to a host (" + std::to_string(request.data.size() >> 20) +
                                                  " MiB, at most " + std::to_string(kMaxWriteBytes >> 20) + ")"}));
        return 0;
    }
    const std::chrono::milliseconds limit = WriteIdleLimit(request.data.size());
    return connection.Endpoint().Request(std::string(host::kFsWrite), host::ToJson(request), StatHandlers(std::move(done)), limit);
}

std::uint64_t ListDirectory(Connection& connection, const std::filesystem::path& path, std::size_t maxEntries,
                            std::function<void(FsResult<std::vector<host::DirEntry>>)> done) {
    struct State {
        std::uint64_t               id = 0;
        std::vector<host::DirEntry> entries;
        std::optional<FsFailure>    rejected;
    };
    auto                state    = std::make_shared<State>();
    auto                finish   = std::make_shared<std::function<void(FsResult<std::vector<host::DirEntry>>)>>(std::move(done));
    protocol::Endpoint& endpoint = connection.Endpoint();
    state->id                    = endpoint.Request(
        std::string(host::kFsReaddir), host::PathParams(path),
        protocol::ResponseHandlers{
            [state, finish](const Json&) {
                if (state->rejected) {
                    (*finish)(std::unexpected(*state->rejected));
                    return;
                }
                (*finish)(std::move(state->entries));
            },
            [state, finish](const protocol::RemoteError& error) { (*finish)(std::unexpected(state->rejected ? *state->rejected : FromRemote(error))); },
            [state, &endpoint, maxEntries, path](const Json& data) {
                if (state->rejected) {
                    return;
                }
                try {
                    std::vector<host::DirEntry> chunk = host::DirEntriesFromJson(data);
                    if (state->entries.size() + chunk.size() <= maxEntries) {
                        std::ranges::move(chunk, std::back_inserter(state->entries));
                        return;
                    }
                    state->rejected = FsFailure{.code    = host::fs_error::kFailed,
                                                .message = path.string() + " holds more than " + std::to_string(maxEntries) + " entries"};
                }
                catch (const protocol::ProtocolError& e) {
                    state->rejected = Malformed(e.what());
                }
                state->entries.clear();
                endpoint.Cancel(state->id);
            }},
        ProtocolRequestTimeoutMs());
    return state->id;
}

std::uint64_t ResolvePath(Connection& connection, const std::filesystem::path& path, std::function<void(FsResult<std::filesystem::path>)> done) {
    auto shared = std::make_shared<std::function<void(FsResult<std::filesystem::path>)>>(std::move(done));
    return connection.Endpoint().Request(
        std::string(host::kFsRealpath), Json{{"path", host::PathToJson(path)}},
        protocol::ResponseHandlers{[shared](const Json& result) {
                                       try {
                                           (*shared)(host::RequirePath(result, "path"));
                                       }
                                       catch (const protocol::ProtocolError& e) {
                                           (*shared)(std::unexpected(Malformed(e.what())));
                                       }
                                   },
                                   [shared](const protocol::RemoteError& error) { (*shared)(std::unexpected(FromRemote(error))); },
                                   {}},
        ProtocolRequestTimeoutMs());
}

std::uint64_t MakeDirectory(Connection& connection, const std::filesystem::path& path, std::function<void(FsResult<host::FileStat>)> done) {
    return connection.Endpoint().Request(std::string(host::kFsMkdir), host::PathParams(path), StatHandlers(std::move(done)), ProtocolRequestTimeoutMs());
}

std::uint64_t RenamePath(Connection& connection, host::FsRenameRequest request, std::function<void(FsResult<void>)> done) {
    return connection.Endpoint().Request(std::string(host::kFsRename), host::ToJson(request), DoneHandlers(std::move(done)), ProtocolRequestTimeoutMs());
}

std::uint64_t RemovePath(Connection& connection, const std::filesystem::path& path, std::function<void(FsResult<void>)> done) {
    return connection.Endpoint().Request(std::string(host::kFsRemove), host::PathParams(path), DoneHandlers(std::move(done)), ProtocolRequestTimeoutMs());
}

} // namespace ned::editor::remote
