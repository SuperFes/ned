//
// Project search on a connected host, through the agent's search/project.
//

#ifndef NED_EDITOR_REMOTE_SEARCH_H
#define NED_EDITOR_REMOTE_SEARCH_H

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "Editor/Host/SearchCodec.h"
#include "Editor/Lifetime.h"
#include "Editor/Remote/Connection.h"
#include "Text/BufferList.h"
#include "Text/RemoteFile.h"

namespace ned::editor::remote {

// onMatches receives each chunk as it arrives, every file already checked
// to lie under the request's root. onDone runs once, with nullopt when the
// search finished and otherwise why it didn't; a chunk that fails the check
// cancels the search and is that reason. Returns the request id, for
// Endpoint::Cancel.
std::uint64_t SearchProject(Connection& connection, const host::SearchRequest& request,
                            std::function<void(std::vector<SearchMatch> matches)>   onMatches,
                            std::function<void(std::optional<std::string> failure)> onDone);

// A search's unsaved remote buffers: the text a search on root's host
// should see in place of what's on its disk.
struct RemoteLive {
    std::vector<LiveContent>      inlined;
    std::vector<host::LiveUpload> uploaded;
};

// What a request carries inline at most, the rest going up as uploads
// (from fs version 6; an older agent sees the disk's text for the rest).
inline constexpr std::size_t kMaxInlineLiveBytes = std::size_t{16} << 20;

// Every modified remote buffer on root's host under root, each with its
// real path there from fs/realpath, so a symlinked spelling still matches
// the files the search walks. done runs once, on the main thread, with
// what to send, every upload already held by the host; not at all once
// token has died. Main thread only.
void GatherRemoteLive(Connection& connection, const text::BufferList& buffers, const text::RemoteFile& root, LifetimeToken token,
                      std::function<void(std::expected<RemoteLive, std::string> live)> done, std::size_t maxInline = kMaxInlineLiveBytes);

// A search request for pattern under root, carrying live.
[[nodiscard]] host::SearchRequest SearchRequestWithLive(const text::RemoteFile& root, std::string pattern, RemoteLive live);

} // namespace ned::editor::remote

#endif // NED_EDITOR_REMOTE_SEARCH_H
