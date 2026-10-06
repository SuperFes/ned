#include "Hello.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <stdexcept>

#include "Editor/Host/Codec.h"

namespace ned::editor::host {

using Json = nlohmann::json;

Json ToJson(const HelloRequest& request) {
    return Json{{"protocol", {{"major", request.major}, {"minor", request.minor}}},
                {"codecs", request.codecs},
                {"window", request.window}};
}

HelloRequest HelloRequestFromJson(const Json& json) {
    const Json&  protocol = RequireObject(json, "protocol");
    HelloRequest request;
    request.major  = RequireUnsigned(protocol, "major");
    request.minor  = RequireUnsigned(protocol, "minor");
    request.window = RequireUnsigned(json, "window");
    for (const Json& codec : RequireArray(json, "codecs")) {
        if (!codec.is_string()) {
            throw protocol::ProtocolError("ned: hello codec is not a string");
        }
        request.codecs.push_back(codec.get<std::string>());
    }
    return request;
}

Json ToJson(const HelloResponse& response) {
    const Capabilities& caps = response.capabilities;
    return Json{{"protocol", {{"major", response.major}, {"minor", response.minor}}},
                {"build", response.build},
                {"arch", response.arch},
                {"codec", response.codec},
                {"window", response.window},
                {"capabilities", {{"fs", caps.fs}, {"proc", caps.proc}, {"search", caps.search}, {"watch", caps.watch}}}};
}

HelloResponse HelloResponseFromJson(const Json& json) {
    const Json&   protocol = RequireObject(json, "protocol");
    const Json&   caps     = RequireObject(json, "capabilities");
    HelloResponse response;
    response.major  = RequireUnsigned(protocol, "major");
    response.minor  = RequireUnsigned(protocol, "minor");
    response.build  = RequireString(json, "build");
    response.arch   = RequireString(json, "arch");
    response.codec  = RequireString(json, "codec");
    response.window = RequireUnsigned(json, "window");
    // Absent means not offered, so an older host's answer still decodes.
    const auto version    = [&caps](const char* name) { return caps.contains(name) ? RequireUnsigned(caps, name) : 0; };
    response.capabilities = Capabilities{version("fs"), version("proc"), version("search"), version("watch")};
    return response;
}

std::string ChooseCodec(const std::vector<std::string>& offered) {
    return std::ranges::find(offered, kCodecZstd) != offered.end() ? std::string(kCodecZstd) : std::string(kCodecNone);
}

std::string BuildArch() {
#if defined(__x86_64__)
    return "x86_64";
#elif defined(__aarch64__)
    return "aarch64";
#else
#error "ned: add this architecture's uname -m spelling"
#endif
}

std::string FileBuildId(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("ned: cannot read " + path.string());
    }
    // FNV-1a 64: this only has to tell builds apart, not resist anyone.
    std::uint64_t           hash = 1469598103934665603ULL;
    std::array<char, 65536> buffer{};
    while (in.read(buffer.data(), buffer.size()) || in.gcount() > 0) {
        for (std::streamsize i = 0; i < in.gcount(); ++i) {
            hash ^= static_cast<unsigned char>(buffer[static_cast<std::size_t>(i)]);
            hash *= 1099511628211ULL;
        }
    }
    std::array<char, 17> hex{};
    std::snprintf(hex.data(), hex.size(), "%016llx", static_cast<unsigned long long>(hash));
    return hex.data();
}

} // namespace ned::editor::host
