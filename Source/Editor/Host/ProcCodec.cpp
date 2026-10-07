#include "ProcCodec.h"

#include <algorithm>
#include <array>
#include <csignal>
#include <stdexcept>

#include "Editor/Host/Codec.h"
#include "Editor/Protocol/Endpoint.h"

namespace ned::editor::host {

using Json = nlohmann::json;

namespace {

    constexpr std::array<std::pair<process::StderrMode, std::string_view>, 3> kStderrNames{{
        {process::StderrMode::Discard, "discard"},
        {process::StderrMode::MergeWithStdout, "merge"},
        {process::StderrMode::Capture, "capture"},
    }};

    constexpr std::array<std::pair<int, std::string_view>, 4> kSignalNames{{
        {SIGKILL, "kill"},
        {SIGTERM, "term"},
        {SIGINT, "int"},
        {SIGHUP, "hup"},
    }};

    template <typename Value, std::size_t N>
    Value Named(const std::array<std::pair<Value, std::string_view>, N>& names, const std::string& name, const char* what) {
        const auto it = std::ranges::find(names, name, &std::pair<Value, std::string_view>::second);
        if (it == names.end()) {
            throw protocol::ProtocolError(std::string("ned: unknown ") + what + " '" + name + "'");
        }
        return it->first;
    }

    template <typename Value, std::size_t N>
    std::string_view NameOf(const std::array<std::pair<Value, std::string_view>, N>& names, Value value) {
        const auto it = std::ranges::find(names, value, &std::pair<Value, std::string_view>::first);
        if (it == names.end()) {
            throw std::invalid_argument("ned: no wire name for that value");
        }
        return it->second;
    }

} // namespace

Json ToJson(const ProcSpawnRequest& request) {
    Json argv = Json::array();
    for (const std::string& arg : request.argv) {
        argv.push_back(Bytes(arg));
    }
    Json env = Json::object();
    for (const auto& [name, value] : request.env) {
        env[name] = value ? Bytes(*value) : Json(nullptr);
    }
    return Json{{"handle", request.handle},
                {"argv", std::move(argv)},
                {"cwd", request.cwd ? PathToJson(*request.cwd) : Json(nullptr)},
                {"env", std::move(env)},
                {"stderr", NameOf(kStderrNames, request.stderrMode)}};
}

ProcSpawnRequest ProcSpawnRequestFromJson(const Json& json) {
    ProcSpawnRequest request;
    request.handle = RequireUnsigned(json, "handle");
    for (const Json& arg : RequireArray(json, "argv")) {
        request.argv.push_back(RequireBytes(Json{{"argv", arg}}, "argv"));
    }
    if (request.argv.empty() || request.argv.front().empty()) {
        throw protocol::ProtocolError("ned: proc/spawn needs a command");
    }
    if (!json.contains("cwd")) {
        throw protocol::ProtocolError("ned: host message lacks 'cwd'");
    }
    if (!json.at("cwd").is_null()) {
        request.cwd = RequirePath(json, "cwd");
    }
    for (const auto& [name, value] : RequireObject(json, "env").items()) {
        if (name.empty() || name.find_first_of(std::string_view("=\0", 2)) != std::string::npos) {
            throw protocol::ProtocolError("ned: '" + name + "' is not an environment variable name");
        }
        request.env.emplace_back(name, value.is_null() ? std::nullopt : std::optional(RequireBytes(Json{{"value", value}}, "value")));
    }
    std::ranges::sort(request.env);
    request.stderrMode = Named(kStderrNames, RequireString(json, "stderr"), "stderr mode");
    return request;
}

Json ToJson(const ProcOutput& output) {
    return Json{{"fd", output.fd}, {"data", Bytes(output.data)}};
}

ProcOutput ProcOutputFromJson(const Json& json) {
    const std::uint64_t fd = RequireUnsigned(json, "fd");
    if (fd != 1 && fd != 2) {
        throw protocol::ProtocolError("ned: process output on fd " + std::to_string(fd));
    }
    return ProcOutput{.fd = static_cast<int>(fd), .data = RequireBytes(json, "data")};
}

Json ProcExitToJson(std::optional<int> exit) {
    return Json{{"exit", exit ? Json(*exit) : Json(nullptr)}};
}

std::optional<int> ProcExitFromJson(const Json& json) {
    if (!json.is_object() || !json.contains("exit")) {
        throw protocol::ProtocolError("ned: host message lacks 'exit'");
    }
    if (json.at("exit").is_null()) {
        return std::nullopt;
    }
    const std::int64_t exit = RequireInteger(json, "exit");
    if (exit < 0 || exit > 255) {
        throw protocol::ProtocolError("ned: exit status " + std::to_string(exit) + " is out of range");
    }
    return static_cast<int>(exit);
}

Json ToJson(const ProcWriteRequest& request) {
    return Json{{"handle", request.handle}, {"data", Bytes(request.data)}};
}

ProcWriteRequest ProcWriteRequestFromJson(const Json& json) {
    return ProcWriteRequest{.handle = RequireUnsigned(json, "handle"), .data = RequireBytes(json, "data")};
}

Json ToJson(const ProcSignalRequest& request) {
    return Json{{"handle", request.handle}, {"signal", NameOf(kSignalNames, request.signal)}};
}

ProcSignalRequest ProcSignalRequestFromJson(const Json& json) {
    return ProcSignalRequest{.handle = RequireUnsigned(json, "handle"), .signal = Named(kSignalNames, RequireString(json, "signal"), "signal")};
}

Json ProcHandleParams(std::uint64_t handle) {
    return Json{{"handle", handle}};
}

std::uint64_t ProcHandleFromJson(const Json& json) {
    return RequireUnsigned(json, "handle");
}

} // namespace ned::editor::host
