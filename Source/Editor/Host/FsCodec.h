//
// Wire forms for the fs namespace (Docs/ClientServerProtocol.md, section 7).
//
//   fs/stat   params {path}
//             result stat
//   fs/read   params {path}
//             chunk  bytes, at most kFsChunkBytes each
//             result stat, its mtime taken before the read
//   fs/write  params {path, data: bytes | upload: uint, expect?: mtime | null}
//             result stat of what was written
//   fs/upload params {handle: uint, data: bytes}
//             result {}
//             Appends data to the upload handle names, which the editor
//             numbers; a write or a search's live content takes it whole.
//   fs/upload-drop params {handle}
//             result {}
//   fs/tree   params {path}, a directory
//             chunk  [relative path], at most kFsReaddirChunk each
//             result {}
//             Every file under it Project/Tree.h's walk reaches: no
//             dot-directories, nothing git ignores.
//   fs/readdir params {path}
//             chunk  [entry], at most kFsReaddirChunk each
//             result {}
//   fs/realpath params {path}, which may be relative, to the agent's
//             working directory: the login's home
//             result {path}
//   fs/mkdir  params {path}, its missing parents made too
//             result stat
//   fs/rename params {from, to}, refused when to exists
//             result {}
//   fs/remove params {path}, a directory with everything in it
//             result {}
//   fs/project-root params {path, detect: bool}, a file or directory
//             result {path}, Project/Root.h's DetectProjectRoot of it, the
//             VCS walk only with detect
//
//   stat      {kind: "missing" | "file" | "directory" | "other",
//              size: uint, mode: uint, mtime: int | null}
//   entry     {name, kind, link: bool}, kind as stat's, through a symlink
//
// An mtime is nanoseconds since the Unix epoch, as the host's clock reads
// it. A write with expect is refused (kFileChanged) unless the file still
// has that mtime, or, for null, still doesn't exist. Uploads (from fs
// version 6) carry what doesn't fit one frame; they end with the
// connection, and the host holds at most kMaxUploadBytes of them.
//

#ifndef NED_EDITOR_HOST_FSCODEC_H
#define NED_EDITOR_HOST_FSCODEC_H

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace ned::editor::host {

inline constexpr std::string_view kFsStat  = "fs/stat";
inline constexpr std::string_view kFsRead  = "fs/read";
inline constexpr std::string_view kFsWrite = "fs/write";
inline constexpr std::string_view kFsReaddir  = "fs/readdir";
inline constexpr std::string_view kFsRealpath = "fs/realpath";
inline constexpr std::string_view kFsMkdir    = "fs/mkdir";
inline constexpr std::string_view kFsRename   = "fs/rename";
inline constexpr std::string_view kFsRemove   = "fs/remove";
inline constexpr std::string_view kFsProjectRoot = "fs/project-root";

inline constexpr std::string_view kFsTree       = "fs/tree";
inline constexpr std::string_view kFsUpload     = "fs/upload";
inline constexpr std::string_view kFsUploadDrop = "fs/upload-drop";

inline constexpr std::size_t kFsChunkBytes   = 256 * 1024;
inline constexpr std::size_t kMaxUploadBytes = std::size_t{2} << 30;
inline constexpr std::size_t kFsReaddirChunk = 1024;

// Error codes an fs request answers with, past the protocol's own.
namespace fs_error {
    inline constexpr int kNotFound    = -32001;
    inline constexpr int kFileChanged = -32002;
    inline constexpr int kFailed      = -32003;
} // namespace fs_error

enum class FileKind { Missing,
                      File,
                      Directory,
                      Other };

struct FileStat {
    FileKind                                       kind = FileKind::Missing;
    std::uint64_t                                  size = 0;
    std::uint32_t                                  mode = 0; // permission bits
    std::optional<std::filesystem::file_time_type> timestamp;

    [[nodiscard]] bool operator==(const FileStat&) const = default;
};

struct DirEntry {
    std::string name;
    FileKind    kind = FileKind::Missing;
    bool        link = false;

    [[nodiscard]] bool operator==(const DirEntry&) const = default;
};

struct FsWriteRequest {
    std::filesystem::path path;
    std::string           data;
    // Writes this upload instead of data.
    std::optional<std::uint64_t> upload;
    // Unset writes regardless; see expect above.
    std::optional<std::optional<std::filesystem::file_time_type>> expect;
};

struct FsRenameRequest {
    std::filesystem::path from;
    std::filesystem::path to;

    [[nodiscard]] bool operator==(const FsRenameRequest&) const = default;
};

struct FsProjectRootRequest {
    std::filesystem::path path;
    bool                  detect = true;
    // A language's own markers, looked for first when detecting
    // (Project/Root.h's FindMarkerRoot).
    std::vector<std::string> markers;

    [[nodiscard]] bool operator==(const FsProjectRootRequest&) const = default;
};

// The decoders throw protocol::ProtocolError on a malformed field.
[[nodiscard]] nlohmann::json        PathParams(const std::filesystem::path& path);
[[nodiscard]] std::filesystem::path PathParamsFromJson(const nlohmann::json& json);

[[nodiscard]] nlohmann::json ToJson(const FileStat& stat);
[[nodiscard]] FileStat       FileStatFromJson(const nlohmann::json& json);

[[nodiscard]] nlohmann::json ToJson(const FsWriteRequest& request);
[[nodiscard]] FsWriteRequest FsWriteRequestFromJson(const nlohmann::json& json);

struct FsUploadRequest {
    std::uint64_t handle = 0;
    std::string   data;
};

[[nodiscard]] nlohmann::json  ToJson(const FsUploadRequest& request);
[[nodiscard]] FsUploadRequest FsUploadRequestFromJson(const nlohmann::json& json);

[[nodiscard]] nlohmann::json  ToJson(const FsRenameRequest& request);
[[nodiscard]] FsRenameRequest FsRenameRequestFromJson(const nlohmann::json& json);

[[nodiscard]] nlohmann::json       ToJson(const FsProjectRootRequest& request);
[[nodiscard]] FsProjectRootRequest FsProjectRootRequestFromJson(const nlohmann::json& json);

[[nodiscard]] nlohmann::json        DirEntriesToJson(std::span<const DirEntry> entries);
[[nodiscard]] std::vector<DirEntry> DirEntriesFromJson(const nlohmann::json& json);

// fs/tree's chunk: paths relative to the tree's root, as bytes.
[[nodiscard]] nlohmann::json RelativePathsToJson(std::span<const std::filesystem::path> paths);
// Throws ProtocolError for an absolute path or one climbing out with "..".
[[nodiscard]] std::vector<std::filesystem::path> RelativePathsFromJson(const nlohmann::json& json);

// fs/realpath's params, whose path may be relative; empty means ".".
[[nodiscard]] std::filesystem::path RealpathParamsFromJson(const nlohmann::json& json);

} // namespace ned::editor::host

#endif // NED_EDITOR_HOST_FSCODEC_H
