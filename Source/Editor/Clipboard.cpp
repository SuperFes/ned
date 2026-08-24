#include "Clipboard.h"

#include <cstdlib>
#include <mutex>
#include <stdexcept>

#include <unistd.h>

#include "DiagnosticsLog.h"
#include "Process/ChildProcess.h"

namespace ned::editor {

namespace {

    std::mutex g_enabledMutex;
    bool       g_enabled = true;

    std::mutex                              g_copyOverrideMutex;
    std::optional<std::vector<std::string>> g_copyOverride;

    std::mutex                              g_pasteOverrideMutex;
    std::optional<std::vector<std::string>> g_pasteOverride;

    struct PlatformTools {
        std::vector<std::string> copyArgv;
        std::vector<std::string> pasteArgv;
    };

    // Memoized separately from the two overrides above: an unset override
    // should still only scan $PATH/the environment once per process, not
    // once per Resolved*Command() call.
    std::mutex                   g_detectMutex;
    bool                         g_detectResolved = false;
    std::optional<PlatformTools> g_detected;

    bool EnvIsSet(const char* name) {
        const char* value = std::getenv(name);
        return value != nullptr && *value != '\0';
    }

    std::optional<PlatformTools> DetectPlatformTools() {
        const std::lock_guard<std::mutex> lock(g_detectMutex);
        if (g_detectResolved) {
            return g_detected;
        }
        g_detectResolved = true;

        if (EnvIsSet("WAYLAND_DISPLAY") && process::ResolveExecutable("wl-copy") && process::ResolveExecutable("wl-paste")) {
            g_detected = PlatformTools{.copyArgv = {"wl-copy"}, .pasteArgv = {"wl-paste", "-n"}};
            return g_detected;
        }

        if (EnvIsSet("DISPLAY")) {
            if (process::ResolveExecutable("xclip")) {
                g_detected = PlatformTools{.copyArgv  = {"xclip", "-selection", "clipboard", "-in"},
                                           .pasteArgv = {"xclip", "-selection", "clipboard", "-out"}};
                return g_detected;
            }
            if (process::ResolveExecutable("xsel")) {
                g_detected = PlatformTools{.copyArgv  = {"xsel", "--clipboard", "--input"},
                                           .pasteArgv = {"xsel", "--clipboard", "--output"}};
                return g_detected;
            }
        }

        if (process::ResolveExecutable("pbcopy") && process::ResolveExecutable("pbpaste")) {
            g_detected = PlatformTools{.copyArgv = {"pbcopy"}, .pasteArgv = {"pbpaste"}};
            return g_detected;
        }

        // WSL: a real Linux userspace, not a native-Windows build -- see
        // this file's own header comment.
        if (process::ResolveExecutable("clip.exe") && process::ResolveExecutable("powershell.exe")) {
            g_detected = PlatformTools{.copyArgv  = {"clip.exe"},
                                       .pasteArgv = {"powershell.exe", "-NoProfile", "-Command", "Get-Clipboard"}};
            return g_detected;
        }

        return g_detected; // nullopt
    }

    std::string Base64Encode(std::string_view data) {
        static constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

        std::string result;
        result.reserve(((data.size() + 2) / 3) * 4);

        std::size_t i = 0;
        for (; i + 2 < data.size(); i += 3) {
            const auto b0 = static_cast<unsigned char>(data[i]);
            const auto b1 = static_cast<unsigned char>(data[i + 1]);
            const auto b2 = static_cast<unsigned char>(data[i + 2]);
            result += kAlphabet[b0 >> 2];
            result += kAlphabet[((b0 & 0x03) << 4) | (b1 >> 4)];
            result += kAlphabet[((b1 & 0x0F) << 2) | (b2 >> 6)];
            result += kAlphabet[b2 & 0x3F];
        }

        const std::size_t remaining = data.size() - i;
        if (remaining == 1) {
            const auto b0 = static_cast<unsigned char>(data[i]);
            result += kAlphabet[b0 >> 2];
            result += kAlphabet[(b0 & 0x03) << 4];
            result += "==";
        }
        else if (remaining == 2) {
            const auto b0 = static_cast<unsigned char>(data[i]);
            const auto b1 = static_cast<unsigned char>(data[i + 1]);
            result += kAlphabet[b0 >> 2];
            result += kAlphabet[((b0 & 0x03) << 4) | (b1 >> 4)];
            result += kAlphabet[(b1 & 0x0F) << 2];
            result += '=';
        }
        return result;
    }

    void WriteOsc52(std::string_view text) {
        const std::string sequence = BuildOsc52CopySequence(text, EnvIsSet("TMUX"));
        std::size_t       written  = 0;
        while (written < sequence.size()) {
            const ssize_t result = ::write(STDOUT_FILENO, sequence.data() + written, sequence.size() - written);
            if (result <= 0) {
                return; // best-effort -- nothing sensible to retry against a live tty write failure
            }
            written += static_cast<std::size_t>(result);
        }
    }

} // namespace

void SetClipboardEnabled(bool enabled) {
    const std::lock_guard<std::mutex> lock(g_enabledMutex);
    g_enabled = enabled;
}

bool ClipboardEnabled() {
    const std::lock_guard<std::mutex> lock(g_enabledMutex);
    return g_enabled;
}

void SetClipboardCopyCommand(std::vector<std::string> argv) {
    const std::lock_guard<std::mutex> lock(g_copyOverrideMutex);
    if (argv.empty()) {
        g_copyOverride.reset();
    }
    else {
        g_copyOverride = std::move(argv);
    }
}

void SetClipboardPasteCommand(std::vector<std::string> argv) {
    const std::lock_guard<std::mutex> lock(g_pasteOverrideMutex);
    if (argv.empty()) {
        g_pasteOverride.reset();
    }
    else {
        g_pasteOverride = std::move(argv);
    }
}

std::optional<std::vector<std::string>> ResolvedClipboardCopyCommand() {
    {
        const std::lock_guard<std::mutex> lock(g_copyOverrideMutex);
        if (g_copyOverride) {
            return g_copyOverride;
        }
    }
    if (const std::optional<PlatformTools> tools = DetectPlatformTools()) {
        return tools->copyArgv;
    }
    return std::nullopt;
}

std::optional<std::vector<std::string>> ResolvedClipboardPasteCommand() {
    {
        const std::lock_guard<std::mutex> lock(g_pasteOverrideMutex);
        if (g_pasteOverride) {
            return g_pasteOverride;
        }
    }
    if (const std::optional<PlatformTools> tools = DetectPlatformTools()) {
        return tools->pasteArgv;
    }
    return std::nullopt;
}

void CopyToSystemClipboard(std::string_view text) {
    if (!ClipboardEnabled()) {
        return;
    }
    if (const std::optional<std::vector<std::string>> argv = ResolvedClipboardCopyCommand()) {
        try {
            process::ChildProcess child(*argv);
            child.WriteAll(text);
            // Falls out of scope here -- the destructor closes stdin (EOF,
            // the shutdown signal a well-behaved clipboard tool waits for)
            // then waits briefly before escalating, exactly the sequencing
            // a copy needs. No exit code check: a failed shell-out has
            // nothing else to fall back to besides the OSC 52 write below,
            // which always happens regardless.
        }
        catch (const std::runtime_error&) {
            // Not found / spawn failure -- treated identically to "no tool
            // resolved," the OSC 52 write below still happens.
        }
    }
    WriteOsc52(text);
}

std::optional<std::string> PasteFromSystemClipboard(std::chrono::milliseconds readTimeout) {
    if (!ClipboardEnabled()) {
        return std::nullopt;
    }
    const std::optional<std::vector<std::string>> argv = ResolvedClipboardPasteCommand();
    if (!argv) {
        return std::nullopt;
    }
    try {
        process::ChildProcess child(*argv);
        std::string           output;
        while (true) {
            const std::optional<std::string> chunk = child.ReadSome(readTimeout);
            if (!chunk) {
                // subprocess-hang-protection follow-up: no data within
                // readTimeout -- the tool is unresponsive (a real Wayland
                // clipboard-manager failure mode). Kill it (SIGKILL is
                // unblockable, so this is bounded) rather than let a
                // main-thread paste keystroke hang the whole editor.
                child.Kill();
                LogMessage(LogCategory::Subprocess, LogSeverity::Warning, "clipboard paste tool timed out, killed: " + (*argv)[0]);
                return std::nullopt;
            }
            if (chunk->empty()) {
                break; // EOF
            }
            output += *chunk;
        }
        const std::optional<int> exitCode = child.WaitForExit();
        if (exitCode && *exitCode == 0) {
            return output;
        }
    }
    catch (const std::runtime_error&) {
        // Not found / spawn failure.
    }
    return std::nullopt;
}

std::string BuildOsc52CopySequence(std::string_view text, bool wrapForTmux) {
    const std::string sequence = "\x1b]52;c;" + Base64Encode(text) + "\x07";
    if (!wrapForTmux) {
        return sequence;
    }

    // tmux's DCS passthrough convention: wrap in \033Ptmux;...\033\\ with
    // every literal ESC byte inside doubled, or tmux's own parser strips
    // the sequence instead of forwarding it to the real terminal.
    std::string wrapped = "\x1bPtmux;";
    for (const char ch : sequence) {
        if (ch == '\x1b') {
            wrapped += '\x1b';
        }
        wrapped += ch;
    }
    wrapped += "\x1b\\";
    return wrapped;
}

} // namespace ned::editor
