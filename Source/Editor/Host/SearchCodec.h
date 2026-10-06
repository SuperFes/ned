//
// Wire forms for search/project (Docs/ClientServerProtocol.md, section 7).
//
//   params  {root: path, pattern: str, live: [{path, canonical, text: bytes}
//                                             | {path, canonical, matches}]}
//   chunk   [{file: path, line: uint, text: bytes}, ...]
//   result  {}
//

#ifndef NED_EDITOR_HOST_SEARCHCODEC_H
#define NED_EDITOR_HOST_SEARCHCODEC_H

#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "Editor/Project/SearchEngine.h"

namespace ned::editor::host {

inline constexpr std::string_view kSearchProject = "search/project";

struct SearchRequest {
    std::filesystem::path    root;
    std::string              pattern;
    std::vector<LiveContent> live;
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
