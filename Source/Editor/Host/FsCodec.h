//
// Wire forms for the fs namespace (Docs/ClientServerProtocol.md, section 7).
//
//   fs/stat   params {path}
//             result stat
//   fs/read   params {path}
//             chunk  bytes, at most kFsChunkBytes each
//             result stat, its mtime taken before the read
//   fs/write  params {path, data: bytes, expect?: mtime | null}
//             result stat of what was written
//
//   stat      {kind: "missing" | "file" | "directory" | "other",
//              size: uint, mode: uint, mtime: int | null}
//
// An mtime is nanoseconds since the Unix epoch, as the host's clock reads
// it. A write with expect is refused (kFileChanged) unless the file still
// has that mtime, or, for null, still doesn't exist.
//

#ifndef NED_EDITOR_HOST_FSCODEC_H
#define NED_EDITOR_HOST_FSCODEC_H

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace ned::editor::host {

inline constexpr std::string_view kFsStat  = "fs/stat";
inline constexpr std::string_view kFsRead  = "fs/read";
inline constexpr std::string_view kFsWrite = "fs/write";

inline constexpr std::size_t kFsChunkBytes = 256 * 1024;

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

struct FsWriteRequest {
    std::filesystem::path path;
    std::string           data;
    // Unset writes regardless; see expect above.
    std::optional<std::optional<std::filesystem::file_time_type>> expect;
};

// The decoders throw protocol::ProtocolError on a malformed field.
[[nodiscard]] nlohmann::json        PathParams(const std::filesystem::path& path);
[[nodiscard]] std::filesystem::path PathParamsFromJson(const nlohmann::json& json);

[[nodiscard]] nlohmann::json ToJson(const FileStat& stat);
[[nodiscard]] FileStat       FileStatFromJson(const nlohmann::json& json);

[[nodiscard]] nlohmann::json ToJson(const FsWriteRequest& request);
[[nodiscard]] FsWriteRequest FsWriteRequestFromJson(const nlohmann::json& json);

} // namespace ned::editor::host

#endif // NED_EDITOR_HOST_FSCODEC_H
