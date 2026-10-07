//
// Wire forms for the proc namespace (Docs/ClientServerProtocol.md, section 7).
//
//   proc/spawn       params {handle: uint, argv: [bytes], cwd: path | null,
//                            env: {name: bytes | null},
//                            stderr: "discard" | "merge" | "capture"}
//                    chunk  {fd: 1 | 2, data: bytes}
//                    result {exit: int | null}, null when a signal ended it
//   proc/write       params {handle, data: bytes}, answered {} once written
//   proc/close-stdin params {handle}, after every write before it; {}
//   proc/signal      params {handle, signal: "kill" | "term" | "int" | "hup"}; {}
//   proc/release     params {handle}; {}. The editor is done with it: stdin
//                    closes, and the child is killed if it outstays a grace
//                    period.
//
// The editor numbers handles, one per spawn on a connection, so it can write
// before the spawn has answered anything. Cancelling a spawn kills the
// child's process group.
//

#ifndef NED_EDITOR_HOST_PROCCODEC_H
#define NED_EDITOR_HOST_PROCCODEC_H

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "Editor/Process/ChildProcess.h"

namespace ned::editor::host {

inline constexpr std::string_view kProcSpawn      = "proc/spawn";
inline constexpr std::string_view kProcWrite      = "proc/write";
inline constexpr std::string_view kProcCloseStdin = "proc/close-stdin";
inline constexpr std::string_view kProcSignal     = "proc/signal";
inline constexpr std::string_view kProcRelease    = "proc/release";

// Error codes a proc request answers with, past the protocol's own.
namespace proc_error {
    inline constexpr int kNoSuchProcess = -32011;
    inline constexpr int kSpawnFailed   = -32012;
    inline constexpr int kWriteFailed   = -32013;
} // namespace proc_error

struct ProcSpawnRequest {
    std::uint64_t                        handle = 0;
    std::vector<std::string>             argv;
    std::optional<std::filesystem::path> cwd;
    // Sorted by name; nullopt removes the variable.
    std::vector<std::pair<std::string, std::optional<std::string>>> env;
    process::StderrMode                                             stderrMode = process::StderrMode::Discard;

    [[nodiscard]] bool operator==(const ProcSpawnRequest&) const = default;
};

struct ProcOutput {
    int         fd = 1;
    std::string data;

    [[nodiscard]] bool operator==(const ProcOutput&) const = default;
};

struct ProcWriteRequest {
    std::uint64_t handle = 0;
    std::string   data;

    [[nodiscard]] bool operator==(const ProcWriteRequest&) const = default;
};

struct ProcSignalRequest {
    std::uint64_t handle = 0;
    int           signal = 0; // SIGKILL, SIGTERM, SIGINT or SIGHUP

    [[nodiscard]] bool operator==(const ProcSignalRequest&) const = default;
};

// The decoders throw protocol::ProtocolError on a malformed field, and the
// signal encoder on a signal the wire has no name for.
[[nodiscard]] nlohmann::json   ToJson(const ProcSpawnRequest& request);
[[nodiscard]] ProcSpawnRequest ProcSpawnRequestFromJson(const nlohmann::json& json);

[[nodiscard]] nlohmann::json ToJson(const ProcOutput& output);
[[nodiscard]] ProcOutput     ProcOutputFromJson(const nlohmann::json& json);

[[nodiscard]] nlohmann::json     ProcExitToJson(std::optional<int> exit);
[[nodiscard]] std::optional<int> ProcExitFromJson(const nlohmann::json& json);

[[nodiscard]] nlohmann::json   ToJson(const ProcWriteRequest& request);
[[nodiscard]] ProcWriteRequest ProcWriteRequestFromJson(const nlohmann::json& json);

[[nodiscard]] nlohmann::json    ToJson(const ProcSignalRequest& request);
[[nodiscard]] ProcSignalRequest ProcSignalRequestFromJson(const nlohmann::json& json);

// {handle}: close-stdin and release.
[[nodiscard]] nlohmann::json ProcHandleParams(std::uint64_t handle);
[[nodiscard]] std::uint64_t  ProcHandleFromJson(const nlohmann::json& json);

} // namespace ned::editor::host

#endif // NED_EDITOR_HOST_PROCCODEC_H
