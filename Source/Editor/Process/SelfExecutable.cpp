#include "SelfExecutable.h"

#include <algorithm>
#include <cstdlib>

namespace ned::editor::process {

namespace {

    std::optional<std::filesystem::path> AbsoluteEnvironmentPath(const char* name) {
        const char* value = std::getenv(name);
        if (value == nullptr || *value == '\0' || *value != '/') {
            return std::nullopt;
        }
        return std::filesystem::path(value);
    }

    bool IsUnder(const std::filesystem::path& path, const std::filesystem::path& directory) {
        const std::filesystem::path normalPath = path.lexically_normal();
        std::filesystem::path       normalDir  = directory.lexically_normal();
        if (!normalDir.has_filename()) {
            normalDir = normalDir.parent_path();
        }
        const auto [dirEnd, pathEnd] = std::ranges::mismatch(normalDir, normalPath);
        return dirEnd == normalDir.end() && pathEnd != normalPath.end();
    }

} // namespace

std::optional<std::filesystem::path> ResolveSelfExecutable(const SelfExecutableInputs& inputs) {
    if (!inputs.procSelfExe) {
        return std::nullopt;
    }
    if (inputs.appImage && inputs.appDir && IsUnder(*inputs.procSelfExe, *inputs.appDir)) {
        return inputs.appImage;
    }
    return inputs.procSelfExe;
}

SelfExecutableInputs CurrentSelfExecutableInputs() {
    SelfExecutableInputs  inputs{.appImage = AbsoluteEnvironmentPath("APPIMAGE"), .appDir = AbsoluteEnvironmentPath("APPDIR")};
    std::error_code       ec;
    std::filesystem::path exe = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (!ec && !exe.empty()) {
        inputs.procSelfExe = std::move(exe);
    }
    return inputs;
}

std::optional<std::filesystem::path> SelfExecutable() {
    return ResolveSelfExecutable(CurrentSelfExecutableInputs());
}

} // namespace ned::editor::process
