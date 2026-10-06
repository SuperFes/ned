#include "FsCodec.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <utility>

#include "Editor/Host/Codec.h"
#include "Editor/Protocol/Endpoint.h"

namespace ned::editor::host {

using Json = nlohmann::json;

namespace {

    constexpr std::array<std::pair<FileKind, std::string_view>, 4> kKindNames{{
        {FileKind::Missing, "missing"},
        {FileKind::File, "file"},
        {FileKind::Directory, "directory"},
        {FileKind::Other, "other"},
    }};

    Json TimeToJson(const std::optional<std::filesystem::file_time_type>& time) {
        if (!time) {
            return nullptr;
        }
        const auto system = std::chrono::file_clock::to_sys(*time);
        return static_cast<std::int64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(system.time_since_epoch()).count());
    }

    std::optional<std::filesystem::file_time_type> TimeFromJson(const Json& json, const char* field) {
        if (!json.contains(field)) {
            throw protocol::ProtocolError(std::string("ned: host message lacks '") + field + "'");
        }
        if (json.at(field).is_null()) {
            return std::nullopt;
        }
        const std::chrono::sys_time<std::chrono::nanoseconds> system{std::chrono::nanoseconds(RequireInteger(json, field))};
        return std::chrono::time_point_cast<std::filesystem::file_time_type::duration>(std::chrono::file_clock::from_sys(system));
    }

} // namespace

Json PathParams(const std::filesystem::path& path) {
    return Json{{"path", PathToJson(path)}};
}

std::filesystem::path PathParamsFromJson(const Json& json) {
    return RequirePath(json, "path");
}

Json ToJson(const FileStat& stat) {
    std::string_view kind;
    for (const auto& [value, name] : kKindNames) {
        if (value == stat.kind) {
            kind = name;
        }
    }
    return Json{{"kind", kind}, {"size", stat.size}, {"mode", stat.mode}, {"mtime", TimeToJson(stat.timestamp)}};
}

FileStat FileStatFromJson(const Json& json) {
    FileStat          stat;
    const std::string kind = RequireString(json, "kind");
    const auto        it   = std::ranges::find(kKindNames, kind, &std::pair<FileKind, std::string_view>::second);
    if (it == kKindNames.end()) {
        throw protocol::ProtocolError("ned: unknown file kind '" + kind + "'");
    }
    stat.kind                = it->first;
    stat.size                = RequireUnsigned(json, "size");
    const std::uint64_t mode = RequireUnsigned(json, "mode");
    if (mode > 07777) {
        throw protocol::ProtocolError("ned: file mode out of range");
    }
    stat.mode      = static_cast<std::uint32_t>(mode);
    stat.timestamp = TimeFromJson(json, "mtime");
    return stat;
}

Json ToJson(const FsWriteRequest& request) {
    Json json{{"path", PathToJson(request.path)}, {"data", Bytes(request.data)}};
    if (request.expect) {
        json["expect"] = TimeToJson(*request.expect);
    }
    return json;
}

FsWriteRequest FsWriteRequestFromJson(const Json& json) {
    FsWriteRequest request;
    request.path = RequirePath(json, "path");
    request.data = RequireBytes(json, "data");
    if (json.contains("expect")) {
        request.expect = TimeFromJson(json, "expect");
    }
    return request;
}

} // namespace ned::editor::host
