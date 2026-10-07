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

    FileKind KindFromJson(const Json& json) {
        const std::string kind = RequireString(json, "kind");
        const auto        it   = std::ranges::find(kKindNames, kind, &std::pair<FileKind, std::string_view>::second);
        if (it == kKindNames.end()) {
            throw protocol::ProtocolError("ned: unknown file kind '" + kind + "'");
        }
        return it->first;
    }

    std::string_view KindName(FileKind kind) {
        return std::ranges::find(kKindNames, kind, &std::pair<FileKind, std::string_view>::first)->second;
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
    return Json{{"kind", KindName(stat.kind)}, {"size", stat.size}, {"mode", stat.mode}, {"mtime", TimeToJson(stat.timestamp)}};
}

FileStat FileStatFromJson(const Json& json) {
    FileStat stat;
    stat.kind                = KindFromJson(json);
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

Json ToJson(const FsRenameRequest& request) {
    return Json{{"from", PathToJson(request.from)}, {"to", PathToJson(request.to)}};
}

FsRenameRequest FsRenameRequestFromJson(const Json& json) {
    return FsRenameRequest{.from = RequirePath(json, "from"), .to = RequirePath(json, "to")};
}

Json ToJson(const FsProjectRootRequest& request) {
    return Json{{"path", PathToJson(request.path)}, {"detect", request.detect}};
}

FsProjectRootRequest FsProjectRootRequestFromJson(const Json& json) {
    return FsProjectRootRequest{.path = RequirePath(json, "path"), .detect = RequireBool(json, "detect")};
}

Json DirEntriesToJson(std::span<const DirEntry> entries) {
    Json json = Json::array();
    for (const DirEntry& entry : entries) {
        json.push_back(Json{{"name", PathToJson(entry.name)}, {"kind", KindName(entry.kind)}, {"link", entry.link}});
    }
    return json;
}

std::vector<DirEntry> DirEntriesFromJson(const Json& json) {
    if (!json.is_array()) {
        throw protocol::ProtocolError("ned: directory entries are not an array");
    }
    std::vector<DirEntry> entries;
    entries.reserve(json.size());
    for (const Json& item : json) {
        if (!item.is_object()) {
            throw protocol::ProtocolError("ned: a directory entry is not an object");
        }
        DirEntry entry{.name = RequireBytes(item, "name"), .kind = KindFromJson(item), .link = false};
        if (entry.name.empty() || entry.name == "." || entry.name == ".." || entry.name.find_first_of(std::string_view("/\0", 2)) != std::string::npos) {
            throw protocol::ProtocolError("ned: directory entry name '" + entry.name + "' is not a file name");
        }
        if (!item.contains("link") || !item.at("link").is_boolean()) {
            throw protocol::ProtocolError("ned: host message lacks 'link'");
        }
        entry.link = item.at("link").get<bool>();
        entries.push_back(std::move(entry));
    }
    return entries;
}

std::filesystem::path RealpathParamsFromJson(const Json& json) {
    const std::string bytes = RequireBytes(json, "path");
    if (bytes.find('\0') != std::string::npos) {
        throw protocol::ProtocolError("ned: host field 'path' holds a NUL");
    }
    return bytes.empty() ? std::filesystem::path(".") : std::filesystem::path(bytes);
}

} // namespace ned::editor::host
