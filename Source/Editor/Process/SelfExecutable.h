//
// The executable ned runs another copy of itself through: the LSP broker, a
// project switch, the ACP agents' MCP relay and ssh's askpass.
//
// Normally /proc/self/exe. Inside an AppImage that names a file in the
// image's mount, which goes away once the process that mounted it exits, so
// the image itself ($APPIMAGE) is run instead and mounts its own. $APPIMAGE
// counts only while /proc/self/exe is under $APPDIR: a ned started from an
// AppImage ned's terminal inherits both variables.
//

#ifndef NED_EDITOR_PROCESS_SELFEXECUTABLE_H
#define NED_EDITOR_PROCESS_SELFEXECUTABLE_H

#include <filesystem>
#include <optional>

namespace ned::editor::process {

struct SelfExecutableInputs {
    std::optional<std::filesystem::path> procSelfExe;
    std::optional<std::filesystem::path> appImage;
    std::optional<std::filesystem::path> appDir;
};

[[nodiscard]] std::optional<std::filesystem::path> ResolveSelfExecutable(const SelfExecutableInputs& inputs);

// The running process's /proc/self/exe, $APPIMAGE and $APPDIR.
[[nodiscard]] SelfExecutableInputs CurrentSelfExecutableInputs();

// ResolveSelfExecutable(CurrentSelfExecutableInputs()).
[[nodiscard]] std::optional<std::filesystem::path> SelfExecutable();

} // namespace ned::editor::process

#endif // NED_EDITOR_PROCESS_SELFEXECUTABLE_H
