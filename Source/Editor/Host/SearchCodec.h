//
// Wire forms for search/project (Docs/ClientServerProtocol.md, section 7).
//
//   params  {root: path, pattern: str, live: [{path, canonical, text: bytes}
//                                             | {path, canonical, matches}
//                                             | {path, canonical, upload: uint}],
//            threads: uint, editorconfig: bool}
//   chunk   [{file: path, line: uint, text: bytes}, ...]
//   result  {}
//
// Chunks arrive as files finish, in the order a local SearchDirectory
// returns them. A live entry naming an upload (fs/upload, from search
// version 2) is searched as that upload's text, which it consumes. threads and editorconfig are the editor's own settings
// (SearchOptions), so a remote search behaves as a local one would.
//

#ifndef NED_EDITOR_HOST_SEARCHCODEC_H
#define NED_EDITOR_HOST_SEARCHCODEC_H

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "Editor/Project/SearchEngine.h"

namespace ned::editor::host {

inline constexpr std::string_view kSearchProject = "search/project";

// Live content whose text went up as an upload.
struct LiveUpload {
    std::filesystem::path path;
    std::filesystem::path canonicalPath;
    std::uint64_t         upload = 0;
};

struct SearchRequest {
    std::filesystem::path    root;
    std::string              pattern;
    std::vector<LiveContent> live;
    std::vector<LiveUpload>  liveUploads;
    SearchOptions            options = CurrentSearchOptions();
};

// The decoders throw protocol::ProtocolError on a malformed field.
[[nodiscard]] nlohmann::json ToJson(const SearchRequest& request);
[[nodiscard]] SearchRequest  SearchRequestFromJson(const nlohmann::json& json);

[[nodiscard]] nlohmann::json           MatchesToJson(std::span<const SearchMatch> matches);
[[nodiscard]] std::vector<SearchMatch> MatchesFromJson(const nlohmann::json& json);

// MatchesFromJson for a host's answer: also throws unless every file is
// under root, so a hostile host can't point the editor elsewhere.
[[nodiscard]] std::vector<SearchMatch> MatchesFromHost(const nlohmann::json& json, const std::filesystem::path& root);

// Matches split into chunks that each stay well under the frame cap.
[[nodiscard]] std::vector<std::span<const SearchMatch>> ChunkMatches(std::span<const SearchMatch> matches);

} // namespace ned::editor::host

#endif // NED_EDITOR_HOST_SEARCHCODEC_H
