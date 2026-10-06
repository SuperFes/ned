#include "SearchCodec.h"

#include <algorithm>
#include <limits>

#include "Editor/Host/Codec.h"
#include "Editor/Protocol/FrameTransport.h"

namespace ned::editor::host {

using Json = nlohmann::json;

namespace {

    constexpr std::size_t kMatchesPerChunk = 1024;
    constexpr std::size_t kChunkTextBudget = std::size_t{1} << 20;
    // One line alone must still fit a frame; a longer one is cut.
    constexpr std::size_t kMaxLineBytes = protocol::kMaxFrameBytes / 4;

} // namespace

Json ToJson(const SearchRequest& request) {
    Json live = Json::array();
    for (const LiveContent& entry : request.live) {
        Json item{{"path", PathToJson(entry.path)}, {"canonical", PathToJson(entry.canonicalPath)}};
        if (entry.preScanned) {
            item["matches"] = MatchesToJson(*entry.preScanned);
        }
        else {
            item["text"] = Bytes(entry.text);
        }
        live.push_back(std::move(item));
    }
    return Json{{"root", PathToJson(request.root)},
                {"pattern", request.pattern},
                {"live", std::move(live)},
                {"threads", request.options.threads},
                {"editorconfig", request.options.editorConfig}};
}

SearchRequest SearchRequestFromJson(const Json& json) {
    SearchRequest request;
    request.root    = RequirePath(json, "root");
    request.pattern = RequireString(json, "pattern");
    for (const Json& item : RequireArray(json, "live")) {
        LiveContent entry{RequirePath(item, "path"), RequirePath(item, "canonical"), {}, std::nullopt};
        if (item.is_object() && item.contains("matches")) {
            entry.preScanned = MatchesFromJson(item["matches"]);
        }
        else {
            entry.text = RequireBytes(item, "text");
        }
        request.live.push_back(std::move(entry));
    }
    const std::uint64_t threads = RequireUnsigned(json, "threads");
    if (threads == 0) {
        throw protocol::ProtocolError("ned: search threads must be at least 1");
    }
    request.options.threads      = static_cast<unsigned int>(std::min<std::uint64_t>(threads, std::numeric_limits<unsigned int>::max()));
    request.options.editorConfig = RequireBool(json, "editorconfig");
    return request;
}

Json MatchesToJson(std::span<const SearchMatch> matches) {
    Json out = Json::array();
    for (const SearchMatch& match : matches) {
        const std::string_view text(match.lineText.data(), std::min(match.lineText.size(), kMaxLineBytes));
        out.push_back(Json{{"file", PathToJson(match.file)}, {"line", match.lineNumber}, {"text", Bytes(text)}});
    }
    return out;
}

std::vector<SearchMatch> MatchesFromJson(const Json& json) {
    if (!json.is_array()) {
        throw protocol::ProtocolError("ned: search matches are not an array");
    }
    std::vector<SearchMatch> matches;
    matches.reserve(json.size());
    for (const Json& item : json) {
        matches.push_back(SearchMatch{RequirePath(item, "file"), RequireUnsigned(item, "line"), RequireBytes(item, "text")});
    }
    return matches;
}

std::vector<SearchMatch> MatchesFromHost(const Json& json, const std::filesystem::path& root) {
    std::vector<SearchMatch>    matches    = MatchesFromJson(json);
    const std::filesystem::path normalized = root.lexically_normal();
    for (const SearchMatch& match : matches) {
        const std::filesystem::path relative = match.file.lexically_relative(normalized);
        if (relative.empty() || relative == "." || *relative.begin() == "..") {
            throw protocol::ProtocolError("ned: host returned a match outside the search root");
        }
    }
    return matches;
}

std::vector<std::span<const SearchMatch>> ChunkMatches(std::span<const SearchMatch> matches) {
    std::vector<std::span<const SearchMatch>> chunks;
    std::size_t                               start = 0;
    while (start < matches.size()) {
        std::size_t end   = start;
        std::size_t bytes = 0;
        while (end < matches.size() && end - start < kMatchesPerChunk && (end == start || bytes < kChunkTextBudget)) {
            bytes += matches[end].lineText.size() + matches[end].file.native().size();
            ++end;
        }
        chunks.push_back(matches.subspan(start, end - start));
        start = end;
    }
    return chunks;
}

} // namespace ned::editor::host
