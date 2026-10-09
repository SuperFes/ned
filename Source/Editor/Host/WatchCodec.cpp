#include "WatchCodec.h"

#include <algorithm>
#include <array>
#include <string>
#include <utility>

#include "Editor/Host/Codec.h"
#include "Editor/Protocol/Endpoint.h"

namespace ned::editor::host {

using Json = nlohmann::json;

namespace {

    constexpr std::array<std::pair<FileEvent::Kind, std::string_view>, 3> kKindNames{{
        {FileEvent::Kind::Created, "created"},
        {FileEvent::Kind::Changed, "changed"},
        {FileEvent::Kind::Deleted, "deleted"},
    }};

    Json PathsToJson(const std::vector<std::filesystem::path>& paths) {
        Json json = Json::array();
        for (const std::filesystem::path& path : paths) {
            json.push_back(PathToJson(path));
        }
        return json;
    }

    std::vector<std::filesystem::path> PathsFromJson(const Json& json, const char* field) {
        std::vector<std::filesystem::path> paths;
        for (const Json& item : RequireArray(json, field)) {
            paths.push_back(RequirePath(Json{{field, item}}, field));
        }
        return paths;
    }

} // namespace

Json ToJson(const WatchSetRequest& request) {
    return Json{{"files", PathsToJson(request.files)}, {"directories", PathsToJson(request.directories)}, {"trees", PathsToJson(request.trees)}};
}

WatchSetRequest WatchSetRequestFromJson(const Json& json) {
    return WatchSetRequest{.files       = PathsFromJson(json, "files"),
                           .directories = PathsFromJson(json, "directories"),
                           .trees       = json.contains("trees") ? PathsFromJson(json, "trees") : std::vector<std::filesystem::path>{}};
}

Json ToJson(const WatchEvents& events) {
    Json list = Json::array();
    for (const FileEvent& event : events.events) {
        const auto kind = std::ranges::find(kKindNames, event.kind, &std::pair<FileEvent::Kind, std::string_view>::first);
        list.push_back(Json{{"path", PathToJson(event.path)}, {"kind", kind->second}});
    }
    Json moves = Json::array();
    for (const FileMove& move : events.moves) {
        moves.push_back(Json{{"from", PathToJson(move.from)}, {"to", PathToJson(move.to)}});
    }
    return Json{{"events", std::move(list)}, {"moves", std::move(moves)}, {"overflow", events.overflow}};
}

WatchEvents WatchEventsFromJson(const Json& json) {
    WatchEvents events{.events = {}, .moves = {}, .overflow = RequireBool(json, "overflow")};
    // An agent before watch capability 2 sends none.
    if (json.contains("moves")) {
        for (const Json& item : RequireArray(json, "moves")) {
            events.moves.push_back(FileMove{.from = RequirePath(item, "from"), .to = RequirePath(item, "to")});
        }
    }
    for (const Json& item : RequireArray(json, "events")) {
        const std::string kind = RequireString(item, "kind");
        const auto        it   = std::ranges::find(kKindNames, kind, &std::pair<FileEvent::Kind, std::string_view>::second);
        if (it == kKindNames.end()) {
            throw protocol::ProtocolError("ned: unknown watch event kind '" + kind + "'");
        }
        events.events.push_back(FileEvent{.path = RequirePath(item, "path"), .kind = it->first});
    }
    return events;
}

} // namespace ned::editor::host
