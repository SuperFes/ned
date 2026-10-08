#include "PtyProcess.h"

#include <utility>

#include "Editor/Process/Pty.h"

namespace ned::editor::terminal {

PtyProcess::PtyProcess(std::vector<std::string> argv, int rows, int cols, ned::ui::EventLoop& eventLoop,
                       std::function<void(std::string_view)> onOutput, std::function<void(std::optional<int>)> onExit,
                       const std::vector<std::pair<std::string, std::string>>& env) : stream_(process::SpawnOnPty(argv, process::TerminalSize{.rows = rows, .cols = cols}, process::SpawnOptions{.env = ShellEnvironment(env)}), eventLoop, std::move(onOutput), std::move(onExit)) {
}

PtyProcess::PtyProcess(process::ChildProcess child, editor::PostQueue& postQueue, std::function<void(std::string_view)> onOutput, std::function<void(std::optional<int>)> onExit) : stream_(std::move(child), postQueue, std::move(onOutput), std::move(onExit)) {
}

std::vector<std::pair<std::string, std::optional<std::string>>> PtyProcess::ShellEnvironment(const std::vector<std::pair<std::string, std::string>>& env) {
    std::vector<std::pair<std::string, std::optional<std::string>>> shellEnv;
    for (const auto& [name, value] : env) {
        if (name != "TERM") {
            shellEnv.emplace_back(name, value);
        }
    }
    // What Emulator understands, whatever the editor itself runs under.
    shellEnv.emplace_back("TERM", "xterm-256color");
    return shellEnv;
}

void PtyProcess::Write(std::string_view data) {
    stream_.Send(std::string(data));
}

void PtyProcess::Resize(int rows, int cols) const noexcept {
    stream_.Child().ResizeTerminal(process::TerminalSize{.rows = rows, .cols = cols});
}

void PtyProcess::DispatchOutput(std::string_view chunk) {
    stream_.DispatchOutput(chunk);
}

void PtyProcess::DispatchExit(std::optional<int> exitCode) {
    stream_.DispatchExit(exitCode);
}

int PtyProcess::MasterFdForTesting() const noexcept {
    return stream_.Child().WriteFd();
}

} // namespace ned::editor::terminal
