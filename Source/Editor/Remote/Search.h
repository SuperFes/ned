//
// Project search on a connected host, through the agent's search/project.
//

#ifndef NED_EDITOR_REMOTE_SEARCH_H
#define NED_EDITOR_REMOTE_SEARCH_H

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "Editor/Host/SearchCodec.h"
#include "Editor/Remote/Connection.h"

namespace ned::editor::remote {

// onMatches receives each chunk as it arrives, every file already checked
// to lie under the request's root. onDone runs once, with nullopt when the
// search finished and otherwise why it didn't; a chunk that fails the check
// cancels the search and is that reason. Returns the request id, for
// Endpoint::Cancel.
std::uint64_t SearchProject(Connection& connection, const host::SearchRequest& request,
                            std::function<void(std::vector<SearchMatch> matches)>   onMatches,
                            std::function<void(std::optional<std::string> failure)> onDone);

} // namespace ned::editor::remote

#endif // NED_EDITOR_REMOTE_SEARCH_H
