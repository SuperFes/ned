#include "FormatOnSave.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <vector>

#include <unistd.h>

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

    // Creates a uniquely-named, already-existing empty temp file via mkstemp
    // (avoids the classic TOCTOU race of "pick a name, hope nothing else
    // grabs it first"). Returns nullopt on failure.
    std::optional<std::filesystem::path> MakeTempFile() {
        const std::string templatePath = (std::filesystem::temp_directory_path() / "ned-format-XXXXXX").string();
        std::vector<char> buffer(templatePath.begin(), templatePath.end());
        buffer.push_back('\0');

        const int fd = ::mkstemp(buffer.data());
        if (fd == -1) {
            return std::nullopt;
        }
        ::close(fd);
        return std::filesystem::path(buffer.data());
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

std::optional<std::string> RunFormatCommand(std::string_view text) {
    const std::optional<std::string> command = FormatCommand();
    if (!command || command->empty()) {
        return std::nullopt;
    }

    const std::optional<std::filesystem::path> inputPath  = MakeTempFile();
    const std::optional<std::filesystem::path> outputPath = MakeTempFile();
    if (!inputPath || !outputPath) {
        return std::nullopt;
    }

    // Cleaned up on every exit path, success or failure alike.
    struct TempFileGuard {
        std::filesystem::path path;
        ~TempFileGuard() {
            std::error_code ec;
            std::filesystem::remove(path, ec);
        }
    };
    const TempFileGuard inputGuard{*inputPath};
    const TempFileGuard outputGuard{*outputPath};

    {
        std::ofstream input(*inputPath, std::ios::binary | std::ios::trunc);
        if (!input) {
            return std::nullopt;
        }
        input.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!input) {
            return std::nullopt;
        }
    }

    // Quoted so a TMPDIR containing spaces (uncommon, but not impossible)
    // doesn't break the shell parse. The command itself is intentionally
    // unquoted/unescaped -- it's the user's own init.janet-configured shell
    // command, not untrusted input, the same trust boundary init.janet
    // already crosses by running arbitrary native code via Janet's FFI.
    const std::string shellCommand =
        *command + " < '" + inputPath->string() + "' > '" + outputPath->string() + "' 2>/dev/null";
    const int status = std::system(shellCommand.c_str());
    if (status != 0) {
        return std::nullopt;
    }

    std::ifstream output(*outputPath, std::ios::binary);
    if (!output) {
        return std::nullopt;
    }
    std::ostringstream contents;
    contents << output.rdbuf();
    if (contents.str().empty()) {
        return std::nullopt;
    }
    return contents.str();
}

} // namespace ned::editor
