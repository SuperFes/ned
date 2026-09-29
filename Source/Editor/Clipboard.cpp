#include "Clipboard.h"

#include <cstdlib>
#include <mutex>
#include <stdexcept>

#include <unistd.h>

#include "DiagnosticsLog.h"
#include "Process/ChildProcess.h"
#include "Text/Base64.h"

namespace ned::editor {

namespace {

    std::mutex g_enabledMutex;
    bool       g_enabled      = true;
    bool       g_osc52Enabled = true; // see Clipboard.h's own comment on SetOsc52Enabled

    std::mutex                              g_copyOverrideMutex;
    std::optional<std::vector<std::string>> g_copyOverride;

    std::mutex                              g_pasteOverrideMutex;
    std::optional<std::vector<std::string>> g_pasteOverride;

    std::mutex                              g_primaryPasteOverrideMutex;
    std::optional<std::vector<std::string>> g_primaryPasteOverride;

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

    // Memoized separately from DetectPlatformTools -- deliberately narrower
    // (Wayland-only, see Clipboard.h's own doc comment on
    // ResolvedPrimarySelectionPasteCommand).
    std::mutex                              g_primaryDetectMutex;
    bool                                    g_primaryDetectResolved = false;
    std::optional<std::vector<std::string>> g_primaryDetected;

    std::optional<std::vector<std::string>> DetectPrimarySelectionTool() {
        const std::lock_guard<std::mutex> lock(g_primaryDetectMutex);
        if (g_primaryDetectResolved) {
            return g_primaryDetected;
        }
        g_primaryDetectResolved = true;

        if (EnvIsSet("WAYLAND_DISPLAY") && process::ResolveExecutable("wl-paste")) {
            g_primaryDetected = std::vector<std::string>{"wl-paste", "--primary", "-n"};
        }
        return g_primaryDetected;
    }

    // Shared by PasteFromSystemClipboard/PasteFromPrimarySelection: spawns
    // argv, drains it via ReadSome(readTimeout) until EOF (killing and
    // returning nullopt on an idle timeout -- subprocess-hang-protection
    // follow-up, see PasteFromSystemClipboard's own doc comment), and
    // returns the accumulated output only on a clean (exit code 0) exit.
    std::optional<std::string> RunPasteCommand(const std::vector<std::string>& argv, std::chrono::milliseconds readTimeout,
                                               std::string_view toolLabel) {
        try {
            process::ChildProcess child(argv);
            std::string           output;
            while (true) {
                const std::optional<std::string> chunk = child.ReadSome(readTimeout);
                if (!chunk) {
                    child.Kill();
                    LogMessage(LogCategory::Subprocess, LogSeverity::Warning,
                               std::string(toolLabel) + " tool timed out, killed: " + argv[0]);
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

    void WriteOsc52(std::string_view text) {
        if (!Osc52Enabled()) {
            return;
        }
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

void SetOsc52Enabled(bool enabled) {
    const std::lock_guard<std::mutex> lock(g_enabledMutex);
    g_osc52Enabled = enabled;
}

bool Osc52Enabled() {
    const std::lock_guard<std::mutex> lock(g_enabledMutex);
    return g_osc52Enabled;
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

void SetClipboardPrimaryPasteCommand(std::vector<std::string> argv) {
    const std::lock_guard<std::mutex> lock(g_primaryPasteOverrideMutex);
    if (argv.empty()) {
        g_primaryPasteOverride.reset();
    }
    else {
        g_primaryPasteOverride = std::move(argv);
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

std::optional<std::vector<std::string>> ResolvedPrimarySelectionPasteCommand() {
    {
        const std::lock_guard<std::mutex> lock(g_primaryPasteOverrideMutex);
        if (g_primaryPasteOverride) {
            return g_primaryPasteOverride;
        }
    }
    return DetectPrimarySelectionTool();
}

void CopyToSystemClipboard(std::string_view text) {
    if (!ClipboardEnabled()) {
        return;
    }
    if (const std::optional<std::vector<std::string>> argv = ResolvedClipboardCopyCommand()) {
        try {
            process::ChildProcess child(*argv);
            child.WriteAll(text, SubprocessWriteTimeoutMs());
            // Falls out of scope here -- the destructor closes stdin (EOF,
            // the shutdown signal a well-behaved clipboard tool waits for)
            // then waits briefly before escalating, exactly the sequencing
            // a copy needs. No exit code check: a failed shell-out has
            // nothing else to fall back to besides the OSC 52 write below,
            // which always happens regardless.
        }
        catch (const std::runtime_error&) {
            // Not found / spawn failure, or (write-side-hang-protection
            // follow-up) the tool stalled and stopped draining stdin --
            // treated identically to "no tool resolved," the OSC 52 write
            // below still happens.
        }
    }
    WriteOsc52(text);
}

std::optional<std::string> PasteFromSystemClipboard(std::chrono::milliseconds readTimeout) {
    if (!ClipboardEnabled()) {
        return std::nullopt;
    }
    // subprocess-hang-protection follow-up: RunPasteCommand kills and
    // returns nullopt on no data within readTimeout -- the tool is
    // unresponsive (a real Wayland clipboard-manager failure mode) --
    // rather than let a main-thread paste keystroke hang the whole editor.
    const std::optional<std::vector<std::string>> argv = ResolvedClipboardPasteCommand();
    if (!argv) {
        return std::nullopt;
    }
    return RunPasteCommand(*argv, readTimeout, "clipboard paste");
}

std::optional<std::string> PasteFromPrimarySelection(std::chrono::milliseconds readTimeout) {
    if (!ClipboardEnabled()) {
        return std::nullopt;
    }
    const std::optional<std::vector<std::string>> argv = ResolvedPrimarySelectionPasteCommand();
    if (!argv) {
        return std::nullopt;
    }
    return RunPasteCommand(*argv, readTimeout, "primary-selection paste");
}

std::optional<std::string> PreferredImageMimeType(std::string_view offeredTypes) {
    std::optional<std::string> fallback;
    while (!offeredTypes.empty()) {
        const std::size_t newline = offeredTypes.find('\n');
        std::string_view  type    = offeredTypes.substr(0, newline);
        offeredTypes              = newline == std::string_view::npos ? std::string_view() : offeredTypes.substr(newline + 1);
        while (!type.empty() && (type.back() == '\r' || type.back() == ' ')) {
            type.remove_suffix(1);
        }
        if (type == "image/png") {
            return std::string(type);
        }
        if (!fallback && type.starts_with("image/")) {
            fallback = std::string(type);
        }
    }
    return fallback;
}

std::optional<ClipboardImage> PasteImageFromSystemClipboard(std::chrono::milliseconds readTimeout) {
    if (!ClipboardEnabled()) {
        return std::nullopt;
    }
    const std::optional<std::vector<std::string>> paste = ResolvedClipboardPasteCommand();
    if (!paste || paste->empty()) {
        return std::nullopt;
    }
    std::vector<std::string> listArgv;
    std::vector<std::string> fetchArgv;
    if ((*paste)[0] == "wl-paste") {
        listArgv  = {"wl-paste", "--list-types"};
        fetchArgv = {"wl-paste", "--type"};
    }
    else if ((*paste)[0] == "xclip") {
        listArgv  = {"xclip", "-selection", "clipboard", "-target", "TARGETS", "-out"};
        fetchArgv = {"xclip", "-selection", "clipboard", "-out", "-target"};
    }
    else {
        return std::nullopt;
    }
    const std::optional<std::string> types = RunPasteCommand(listArgv, readTimeout, "clipboard type list");
    if (!types) {
        return std::nullopt;
    }
    const std::optional<std::string> mimeType = PreferredImageMimeType(*types);
    if (!mimeType) {
        return std::nullopt;
    }
    fetchArgv.push_back(*mimeType);
    std::optional<std::string> bytes = RunPasteCommand(fetchArgv, readTimeout, "clipboard image paste");
    if (!bytes || bytes->empty()) {
        return std::nullopt;
    }
    return ClipboardImage{.mimeType = *mimeType, .bytes = std::move(*bytes)};
}

std::optional<std::vector<std::string>> ImageCopyCommand(const std::vector<std::string>& copyArgv, std::string_view mimeType) {
    if (copyArgv.empty()) {
        return std::nullopt;
    }
    if (copyArgv[0] == "wl-copy") {
        return std::vector<std::string>{"wl-copy", "--type", std::string(mimeType)};
    }
    if (copyArgv[0] == "xclip") {
        return std::vector<std::string>{"xclip", "-selection", "clipboard", "-target", std::string(mimeType), "-in"};
    }
    return std::nullopt;
}

bool CopyImageToSystemClipboard(std::string_view mimeType, std::string_view bytes) {
    if (!ClipboardEnabled()) {
        return false;
    }
    const std::optional<std::vector<std::string>> copy = ResolvedClipboardCopyCommand();
    if (!copy) {
        return false;
    }
    const std::optional<std::vector<std::string>> argv = ImageCopyCommand(*copy, mimeType);
    if (!argv) {
        return false;
    }
    try {
        process::ChildProcess child(*argv);
        child.WriteAll(bytes, SubprocessWriteTimeoutMs());
    }
    catch (const std::runtime_error&) {
        return false;
    }
    return true;
}

std::string BuildOsc52CopySequence(std::string_view text, bool wrapForTmux) {
    const std::string sequence = "\x1b]52;c;" + text::Base64Encode(text) + "\x07";
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
