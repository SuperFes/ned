#include "TerminalTabLauncher.h"

#include <cctype>
#include <cstdlib>
#include <fstream>

#include "Process/ChildProcess.h"

namespace ned::editor {

namespace {

    std::string_view Trim(std::string_view text) {
        std::size_t start = 0;
        while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start])) != 0) {
            ++start;
        }
        std::size_t end = text.size();
        while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
            --end;
        }
        return text.substr(start, end - start);
    }

    // POSIX single-quote shell escaping -- Konsole's runCommand is the one
    // mechanism in this whole file that has no choice but to go through a
    // real shell (it works by typing text into a terminal prompt), unlike
    // every other handler here, which spawns a real argv directly.
    std::string ShellQuoteSingle(const std::filesystem::path& path) {
        std::string quoted = "'";
        for (const char c : path.string()) {
            if (c == '\'') {
                quoted += "'\\''";
            }
            else {
                quoted += c;
            }
        }
        quoted += "'";
        return quoted;
    }

    std::optional<std::string> RealEnvLookup(std::string_view name) {
        const std::string key(name);
        const char*       value = std::getenv(key.c_str());
        if (value != nullptr && *value != '\0') {
            return std::string(value);
        }
        return std::nullopt;
    }

    bool RealKonsoleDbusApiEnabled() {
        std::filesystem::path path;
        if (const char* xdgConfigHome = std::getenv("XDG_CONFIG_HOME"); xdgConfigHome != nullptr && *xdgConfigHome != '\0') {
            path = std::filesystem::path(xdgConfigHome) / "konsolerc";
        }
        else if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
            path = std::filesystem::path(home) / ".config" / "konsolerc";
        }
        else {
            return false;
        }

        std::ifstream file(path, std::ios::binary);
        if (!file) {
            return false;
        }
        const std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        return KonsoleDbusApiEnabledFromConfig(content);
    }

    // Runs argv to completion, discarding its output -- for a handler whose
    // success is fully determined by its exit code (every plain single-spawn
    // handler, plus Konsole's own runCommand step).
    bool RunArgv(const std::vector<std::string>& argv) {
        try {
            process::ChildProcess child(argv);
            while (!child.ReadSome().empty()) {
                // Drain stdout so the child never blocks writing to a full
                // pipe -- same reasoning as any other ChildProcess consumer
                // that doesn't care about the output itself.
            }
            const std::optional<int> exitCode = child.WaitForExit();
            return exitCode.has_value() && *exitCode == 0;
        }
        catch (const std::exception&) {
            return false;
        }
    }

    // Runs argv to completion and returns its trimmed stdout -- Konsole's
    // newSession step needs this to learn the new session's id before
    // building the runCommand step's own argv. nullopt on spawn failure or
    // a nonzero exit.
    std::optional<std::string> RunArgvCapturingStdout(const std::vector<std::string>& argv) {
        try {
            process::ChildProcess child(argv);
            std::string           output;
            for (std::string chunk = child.ReadSome(); !chunk.empty(); chunk = child.ReadSome()) {
                output += chunk;
            }
            const std::optional<int> exitCode = child.WaitForExit();
            if (!exitCode.has_value() || *exitCode != 0) {
                return std::nullopt;
            }
            return std::string(Trim(output));
        }
        catch (const std::exception&) {
            return std::nullopt;
        }
    }

    // Konsole is the one handler needing two dependent D-Bus calls rather
    // than a single spawn -- see this file's header comment. Reads
    // $KONSOLE_DBUS_SERVICE/$KONSOLE_DBUS_WINDOW directly (not through
    // DetectTerminal's EnvLookup) since it's only ever reached from the real
    // production TryOpenInNewTab, never from a test.
    bool TryOpenKonsoleTab(const std::filesystem::path& nedExecutable, const std::filesystem::path& root) {
        const char* service = std::getenv("KONSOLE_DBUS_SERVICE");
        const char* window  = std::getenv("KONSOLE_DBUS_WINDOW");
        if (service == nullptr || *service == '\0' || window == nullptr || *window == '\0') {
            return false; // SelectTerminal said "Konsole," but the env vars it actually needs aren't there
        }

        std::optional<std::string> qdbus = process::ResolveExecutable("qdbus6");
        if (!qdbus) {
            qdbus = process::ResolveExecutable("qdbus");
        }
        if (!qdbus) {
            return false;
        }

        const std::optional<std::string> sessionId =
            RunArgvCapturingStdout(BuildKonsoleNewSessionArgv(*qdbus, service, window, root));
        if (!sessionId || sessionId->empty()) {
            return false;
        }

        return RunArgv(BuildKonsoleRunCommandArgv(*qdbus, service, *sessionId, nedExecutable, root));
    }

} // namespace

std::optional<TerminalKind> SelectTerminal(const EnvLookup& env, bool konsoleDbusApiEnabled) {
    if (env("TMUX")) {
        return TerminalKind::Tmux;
    }
    if (env("STY")) {
        return TerminalKind::Screen;
    }
    if (env("KONSOLE_VERSION") && konsoleDbusApiEnabled) {
        return TerminalKind::Konsole;
    }
    if (env("GNOME_TERMINAL_SCREEN") || env("VTE_VERSION")) {
        return TerminalKind::GnomeTerminal;
    }
    if (env("WEZTERM_PANE")) {
        return TerminalKind::WezTerm;
    }
    if (env("GHOSTTY_RESOURCES_DIR")) {
        return TerminalKind::Ghostty;
    }
    if (env("KITTY_WINDOW_ID")) {
        return TerminalKind::Kitty;
    }
    return std::nullopt;
}

bool KonsoleDbusApiEnabledFromConfig(std::string_view konsolercContent) {
    std::string currentSection;
    std::size_t pos = 0;
    while (pos < konsolercContent.size()) {
        const std::size_t eol  = konsolercContent.find('\n', pos);
        std::string_view  line = konsolercContent.substr(pos, eol == std::string_view::npos ? std::string_view::npos : eol - pos);
        pos                    = (eol == std::string_view::npos) ? konsolercContent.size() : eol + 1;
        line                   = Trim(line);

        if (line.size() >= 2 && line.front() == '[' && line.back() == ']') {
            currentSection = std::string(line.substr(1, line.size() - 2));
            continue;
        }
        if (currentSection != "KonsoleWindow") {
            continue;
        }

        const std::size_t equals = line.find('=');
        if (equals == std::string_view::npos) {
            continue;
        }
        if (Trim(line.substr(0, equals)) == "EnableSecuritySensitiveDBusAPI") {
            return Trim(line.substr(equals + 1)) == "true";
        }
    }
    return false; // matches Konsole's own documented default
}

std::optional<TerminalKind> DetectTerminal() {
    return SelectTerminal(RealEnvLookup, RealKonsoleDbusApiEnabled());
}

std::vector<std::string> BuildLaunchArgv(TerminalKind kind, const std::filesystem::path& nedExecutable,
                                         const std::filesystem::path& root) {
    const std::string exe = nedExecutable.string();
    const std::string dir = root.string();
    switch (kind) {
        case TerminalKind::Tmux:
            return {"tmux", "new-window", "-c", dir, exe, dir};
        case TerminalKind::Screen:
            // No chdir equivalent worth the extra complexity for a lower-
            // priority handler -- dir is passed as ned's own argument
            // regardless of the new window's shell cwd (see header comment).
            return {"screen", "-X", "screen", exe, dir};
        case TerminalKind::GnomeTerminal:
            return {"gnome-terminal", "--tab", "--working-directory=" + dir, "--", exe, dir};
        case TerminalKind::WezTerm:
            // Confirmed live: no --new-tab flag exists -- a new tab is the
            // default when --new-window is omitted.
            return {"wezterm", "cli", "spawn", "--cwd", dir, "--", exe, dir};
        case TerminalKind::Ghostty:
            return {"ghostty", "+new-window", "--working-directory=" + dir, "-e", exe, dir};
        case TerminalKind::Kitty:
            // No --to: confirmed via `kitten @ launch --help` that it
            // defaults to $KITTY_LISTEN_ON, else the calling process's own
            // controlling terminal.
            return {"kitten", "@", "launch", "--type=tab", "--cwd", dir, exe, dir};
        case TerminalKind::Konsole:
            return {}; // Konsole uses BuildKonsoleNewSessionArgv/BuildKonsoleRunCommandArgv instead
    }
    return {};
}

std::vector<std::string> BuildKonsoleNewSessionArgv(std::string_view qdbusBinary, std::string_view dbusService,
                                                    std::string_view dbusWindow, const std::filesystem::path& root) {
    return {std::string(qdbusBinary), std::string(dbusService), std::string(dbusWindow), "newSession", "", root.string()};
}

std::vector<std::string> BuildKonsoleRunCommandArgv(std::string_view qdbusBinary, std::string_view dbusService,
                                                    std::string_view sessionId, const std::filesystem::path& nedExecutable,
                                                    const std::filesystem::path& root) {
    const std::string command = ShellQuoteSingle(nedExecutable) + " " + ShellQuoteSingle(root);
    return {std::string(qdbusBinary), std::string(dbusService), "/Sessions/" + std::string(sessionId), "runCommand", command};
}

bool TryOpenInNewTab(const std::filesystem::path& nedExecutable, const std::filesystem::path& root) {
    const std::optional<TerminalKind> kind = DetectTerminal();
    if (!kind) {
        return false;
    }
    if (*kind == TerminalKind::Konsole) {
        return TryOpenKonsoleTab(nedExecutable, root);
    }
    return RunArgv(BuildLaunchArgv(*kind, nedExecutable, root));
}

} // namespace ned::editor
