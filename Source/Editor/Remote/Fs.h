//
// Files on a connected host, through the agent's fs namespace
// (Host/FsCodec.h). Each call answers once, through done, on the main
// thread; a host that goes quiet fails the request (Endpoint::ExpireIdle)
// rather than leaving it pending.
//

#ifndef NED_EDITOR_REMOTE_FS_H
#define NED_EDITOR_REMOTE_FS_H

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <string>

#include "Editor/Host/FsCodec.h"
#include "Editor/Remote/Connection.h"
#include "Text/FileContents.h"

namespace ned::editor::remote {

struct FsFailure {
    // A host::fs_error code, or the protocol's own: kTimedOut,
    // kConnectionClosed, kCancelled.
    int         code = 0;
    std::string message;
};

template <typename T>
using FsResult = std::expected<T, FsFailure>;

// The most a write carries: its content travels in one request, under the
// frame cap.
inline constexpr std::size_t kMaxWriteBytes = std::size_t{48} << 20;

// How long a write may hear nothing: the request timeout, plus the time its
// content takes to go up a slow link.
[[nodiscard]] std::chrono::milliseconds WriteIdleLimit(std::size_t bytes);

// Each returns the request's id, for Endpoint::Cancel; 0 when done has
// already run without sending anything.
std::uint64_t StatFile(Connection& connection, const std::filesystem::path& path, std::function<void(FsResult<host::FileStat>)> done);
// A file whose content runs past maxBytes is cancelled and fails, as is
// one the host answers malformed.
std::uint64_t ReadFile(Connection& connection, const std::filesystem::path& path, std::size_t maxBytes,
                       std::function<void(FsResult<text::FileContents>)> done);
std::uint64_t WriteFile(Connection& connection, host::FsWriteRequest request, std::function<void(FsResult<host::FileStat>)> done);

} // namespace ned::editor::remote

#endif // NED_EDITOR_REMOTE_FS_H
