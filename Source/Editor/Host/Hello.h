//
// The session handshake (Docs/ClientServerProtocol.md, section 4): the
// editor's hello and a host's answer, as plain data with their wire forms.
//

#ifndef NED_EDITOR_HOST_HELLO_H
#define NED_EDITOR_HOST_HELLO_H

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace ned::editor::host {

inline constexpr std::uint64_t kProtocolMajor = 1;
inline constexpr std::uint64_t kProtocolMinor = 0;

inline constexpr std::string_view kHello = "hello";

inline constexpr std::string_view kCodecZstd = "zstd";
inline constexpr std::string_view kCodecNone = "none";

// Each namespace's method-set version; 0 means not offered.
struct Capabilities {
    std::uint64_t fs     = 0;
    std::uint64_t proc   = 0;
    std::uint64_t search = 0;
    std::uint64_t watch  = 0;

    [[nodiscard]] bool operator==(const Capabilities&) const = default;
};

struct HelloRequest {
    std::uint64_t            major = kProtocolMajor;
    std::uint64_t            minor = kProtocolMinor;
    std::vector<std::string> codecs;
    std::size_t              window{};
};

struct HelloResponse {
    std::uint64_t major = kProtocolMajor;
    std::uint64_t minor = kProtocolMinor;
    std::string   build;
    std::string   arch;
    std::string   codec;
    std::size_t   window{};
    Capabilities  capabilities;
};

// The decoders throw protocol::ProtocolError on a missing or mistyped field.
[[nodiscard]] nlohmann::json ToJson(const HelloRequest& request);
[[nodiscard]] HelloRequest   HelloRequestFromJson(const nlohmann::json& json);
[[nodiscard]] nlohmann::json ToJson(const HelloResponse& response);
[[nodiscard]] HelloResponse  HelloResponseFromJson(const nlohmann::json& json);

// zstd when the editor offers it, else none.
[[nodiscard]] std::string ChooseCodec(const std::vector<std::string>& offered);

// The architecture this binary was built for, as uname -m spells it.
[[nodiscard]] std::string BuildArch();

// A content hash of the file at path: an agent's build id. Throws
// std::runtime_error if it can't be read.
[[nodiscard]] std::string FileBuildId(const std::filesystem::path& path);

} // namespace ned::editor::host

#endif // NED_EDITOR_HOST_HELLO_H
