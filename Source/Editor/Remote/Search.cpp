#include "Search.h"

#include <memory>
#include <utility>

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

} // namespace ned::editor::remote
