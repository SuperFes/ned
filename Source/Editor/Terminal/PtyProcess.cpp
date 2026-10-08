#include "PtyProcess.h"

#include <utility>

#include "Editor/Process/Pty.h"

namespace ned::editor::terminal {

namespace {

    process::ChildProcess SpawnShell(const std::vector<std::string>& argv, int rows, int cols, const std::vector<std::pair<std::string, std::string>>& env) {
        process::SpawnOptions options;
        for (const auto& [name, value] : env) {
            if (name != "TERM") {
                options.env.emplace_back(name, value);
            }
        }
        // What Emulator understands, whatever the editor itself runs under.
        options.env.emplace_back("TERM", "xterm-256color");
        return process::SpawnOnPty(argv, process::TerminalSize{.rows = rows, .cols = cols}, options);
    }

} // namespace

PtyProcess::PtyProcess(std::vector<std::string> argv, int rows, int cols, ned::ui::EventLoop& eventLoop,
                       std::function<void(std::string_view)> onOutput, std::function<void(std::optional<int>)> onExit,
                       const std::vector<std::pair<std::string, std::string>>& env) : stream_(SpawnShell(argv, rows, cols, env), eventLoop, std::move(onOutput), std::move(onExit)) {
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
