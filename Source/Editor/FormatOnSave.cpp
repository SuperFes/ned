#include "FormatOnSave.h"

#include <atomic>
#include <csignal>
#include <exception>
#include <mutex>
#include <thread>
#include <utility>

#include "Editor/Process/ChildProcess.h"
#include "Editor/ProcessTimeouts.h"

namespace ned::editor {

namespace {

    std::mutex& CommandMutex() {
        static std::mutex mutex;
        return mutex;
    }

    std::optional<std::string>& CommandStorage() {
        static std::optional<std::string> command;
        return command;
    }

} // namespace

void SetFormatCommand(std::optional<std::string> command) {
    const std::lock_guard<std::mutex> lock(CommandMutex());
    CommandStorage() = std::move(command);
}

std::optional<std::string> FormatCommand() {
    const std::lock_guard<std::mutex> lock(CommandMutex());
    return CommandStorage();
}

std::vector<std::string> FormatCommandArgv(const std::string& command) {
    // The user's own init.janet-configured shell command, not untrusted
    // input: the same trust boundary init.janet already crosses.
    return {"/bin/sh", "-c", command};
}

std::optional<std::string> AcceptFormatOutput(std::string output, std::optional<int> exitCode) {
    if (exitCode != 0 || output.empty()) {
        return std::nullopt;
    }
    return output;
}

std::optional<std::string> RunFormatCommand(std::string_view text) {
    const std::optional<std::string> command = FormatCommand();
    if (!command || command->empty()) {
        return std::nullopt;
    }
    try {
        process::ChildProcess child(FormatCommandArgv(*command), process::StderrMode::Discard);
        std::atomic<bool>     wrote = false;
        // Written alongside the read below: a formatter can print before it
        // has read everything, and stall once nothing takes its output.
        std::jthread writer([&] {
            try {
                child.WriteAll(text, SubprocessWriteTimeoutMs());
                wrote = true;
            }
            catch (const std::exception&) {
                // Ends the read below too.
                child.Signal(SIGKILL);
            }
            child.CloseInput();
        });
        std::string  output;
        for (std::string chunk = child.ReadSome(); !chunk.empty(); chunk = child.ReadSome()) {
            output += chunk;
        }
        writer.join();
        const std::optional<int> exitCode = child.WaitForExit();
        return wrote ? AcceptFormatOutput(std::move(output), exitCode) : std::nullopt;
    }
    catch (const std::exception&) {
        return std::nullopt;
    }
}

} // namespace ned::editor
