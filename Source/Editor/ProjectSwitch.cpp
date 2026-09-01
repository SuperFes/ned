#include "ProjectSwitch.h"

#include <mutex>
#include <system_error>

#include <unistd.h>

#include "PendingReExec.h"
#include "Process/ChildProcess.h"
#include "ProjectRegistry.h"
#include "TerminalTabLauncher.h"

namespace ned::editor {

namespace {

    std::mutex& ProjectOpenCommandMutex() {
        static std::mutex mutex;
        return mutex;
    }

    std::optional<std::vector<std::string>>& ProjectOpenCommandStorage() {
        static std::optional<std::vector<std::string>> command;
        return command;
    }

    // /proc/self/exe: Linux-specific, matching this codebase's existing
    // scope (POSIX throughout, no macOS/BSD support claimed anywhere yet --
    // see ROADMAP.md's own "Native Windows Port" sketch for how large a
    // real cross-platform pass would be). Verified executable, not just
    // resolvable, before ever being handed to execv() -- a switch that
    // can't complete must never begin quitting the current session.
    std::optional<std::filesystem::path> ResolveOwnExecutablePath() {
        char          buffer[4096];
        const ssize_t length = ::readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
        if (length <= 0) {
            return std::nullopt;
        }
        buffer[length] = '\0';

        const std::filesystem::path path(buffer);
        std::error_code             existsEc;
        if (!std::filesystem::exists(path, existsEc) || existsEc) {
            return std::nullopt;
        }
        if (::access(path.c_str(), X_OK) != 0) {
            return std::nullopt;
        }
        return path;
    }

    // Runs argv to completion, true only on a clean exit -- the custom-
    // command tier's own spawn. Mirrors TerminalTabLauncher.cpp's private
    // RunArgv exactly; not shared across the two files since it's a handful
    // of lines, the same "not worth a shared dependency for something this
    // small" call ProjectSession.cpp/ProjectTrust.cpp already make for
    // their own small duplicated helpers.
    bool RunArgv(const std::vector<std::string>& argv) {
        try {
            process::ChildProcess child(argv);
            while (!child.ReadSome().empty()) {
                // Drain stdout so the child never blocks on a full pipe.
            }
            const std::optional<int> exitCode = child.WaitForExit();
            return exitCode.has_value() && *exitCode == 0;
        }
        catch (const std::exception&) {
            return false;
        }
    }

} // namespace

std::vector<std::string> SubstituteProjectOpenCommandArgv(const std::vector<std::string>& argvTemplate,
                                                          const std::filesystem::path&    root) {
    static constexpr std::string_view kPlaceholder = "{root}";
    const std::string                 rootString   = root.string();

    std::vector<std::string> result;
    result.reserve(argvTemplate.size());
    for (const std::string& element : argvTemplate) {
        std::string substituted = element;
        for (std::size_t pos = substituted.find(kPlaceholder); pos != std::string::npos;
             pos             = substituted.find(kPlaceholder, pos + rootString.size())) {
            substituted.replace(pos, kPlaceholder.size(), rootString);
        }
        result.push_back(std::move(substituted));
    }
    return result;
}

void SetProjectOpenCommand(std::vector<std::string> argvTemplate) {
    const std::lock_guard<std::mutex> lock(ProjectOpenCommandMutex());
    if (argvTemplate.empty()) {
        ProjectOpenCommandStorage().reset();
    }
    else {
        ProjectOpenCommandStorage() = std::move(argvTemplate);
    }
}

std::optional<std::vector<std::string>> ProjectOpenCommand() {
    const std::lock_guard<std::mutex> lock(ProjectOpenCommandMutex());
    return ProjectOpenCommandStorage();
}

ProjectActivationOutcome ActivateProjectRoot(const std::filesystem::path& root) {
    std::error_code existsEc;
    if (!std::filesystem::is_directory(root, existsEc) || existsEc) {
        return ProjectActivationOutcome::RootMissing;
    }

    TouchProject(root); // no-op if root isn't registered

    const std::optional<std::filesystem::path> selfExe = ResolveOwnExecutablePath();

    if (selfExe && TryOpenInNewTab(*selfExe, root)) {
        return ProjectActivationOutcome::OpenedInNewTab;
    }

    if (const std::optional<std::vector<std::string>> customCommand = ProjectOpenCommand()) {
        if (RunArgv(SubstituteProjectOpenCommandArgv(*customCommand, root))) {
            return ProjectActivationOutcome::RanCustomCommand;
        }
    }

    if (!selfExe) {
        return ProjectActivationOutcome::Failed;
    }
    SetPendingReExec(PendingReExecRequest{*selfExe, root});
    return ProjectActivationOutcome::ReplacingInPlace;
}

void ResetProjectOpenCommandForTesting() {
    const std::lock_guard<std::mutex> lock(ProjectOpenCommandMutex());
    ProjectOpenCommandStorage().reset();
}

} // namespace ned::editor
