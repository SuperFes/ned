#include "ProjectSearch.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <regex>
#include <stdexcept>

#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

#include "GitIgnore.h"

// posix_spawn's envp argument needs the process's own environment -- POSIX
// guarantees this global exists, just not in a standard header. Same
// declaration Lsp/Transport.cpp's own posix_spawn use already needs.
extern char** environ;

namespace ned::editor {

namespace {

    using Json = nlohmann::json;

    bool IsDotDirectory(const std::filesystem::directory_entry& entry) {
        const std::string name = entry.path().filename().string();
        return !name.empty() && name.front() == '.';
    }

    bool LooksBinary(const std::filesystem::path& path) {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            return true; // unreadable -- not worth searching
        }

        std::array<char, 8192> buffer{};
        file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto bytesRead = static_cast<std::size_t>(file.gcount());
        return std::find(buffer.data(), buffer.data() + bytesRead, '\0') != buffer.data() + bytesRead;
    }

    // The single-threaded C++ scanner -- SearchDirectory's own original
    // implementation, extracted unchanged so it can serve as the fallback
    // when ripgrep isn't available (or fails). regex is passed in already
    // constructed, so SearchDirectory's own "throws std::regex_error first,
    // regardless of which backend actually runs" contract lives in exactly
    // one place.
    std::vector<SearchMatch> SearchWithBuiltinScanner(const std::filesystem::path& absoluteRoot, const std::regex& regex) {
        std::vector<SearchMatch> matches;

        std::error_code ec;
        auto            it = std::filesystem::recursive_directory_iterator(
            absoluteRoot, std::filesystem::directory_options::skip_permission_denied, ec);
        const auto end = std::filesystem::recursive_directory_iterator();
        if (ec) {
            return matches;
        }

        // project-search-hang follow-up: see GitIgnore.h's own header
        // comment for why this exists at all -- without it, a recursive
        // walk from a real project root also scans every file under
        // build/, node_modules/, and similar generated/dependency
        // directories. Only needed here, not on the ripgrep path below --
        // rg already honors .gitignore natively, and more completely
        // (nested files, global gitignore, .git/info/exclude) than this
        // root-only matcher does.
        const GitIgnoreMatcher gitIgnore(absoluteRoot);

        for (; it != end; it.increment(ec)) {
            if (ec) {
                break;
            }

            const std::filesystem::directory_entry& entry    = *it;
            const std::filesystem::path              relative = std::filesystem::relative(entry.path(), absoluteRoot);

            if (entry.is_directory()) {
                if (IsDotDirectory(entry) || gitIgnore.IsIgnored(relative, /*isDirectory=*/true)) {
                    it.disable_recursion_pending();
                }
                continue;
            }
            if (!entry.is_regular_file() || gitIgnore.IsIgnored(relative, /*isDirectory=*/false) || LooksBinary(entry.path())) {
                continue;
            }

            std::ifstream file(entry.path());
            if (!file) {
                continue;
            }

            std::string line;
            std::size_t lineNumber = 0;
            while (std::getline(file, line)) {
                ++lineNumber;
                if (std::regex_search(line, regex)) {
                    matches.push_back(SearchMatch{entry.path(), lineNumber, line});
                }
            }
        }

        return matches;
    }

    // ripgrep-search follow-up. Manual $PATH search -- mirrors
    // Lsp/Transport.cpp's own ResolveExecutable exactly (not shared code:
    // that helper is anonymous-namespace-private to a different subsystem;
    // duplicating ~15 lines here matches this codebase's own established
    // "not worth a cross-file dependency for something this small"
    // precedent, e.g. IsDotDirectory above being its own copy rather than
    // shared with ProjectTree.cpp).
    std::optional<std::filesystem::path> FindRipgrepOnPath() {
        const char* pathEnv = std::getenv("PATH");
        if (pathEnv == nullptr) {
            return std::nullopt;
        }
        const std::string_view path(pathEnv);
        std::size_t            start = 0;
        while (start <= path.size()) {
            const std::size_t      sep = path.find(':', start);
            const std::string_view dir =
                path.substr(start, sep == std::string_view::npos ? std::string_view::npos : sep - start);
            if (!dir.empty()) {
                const std::filesystem::path candidate = std::filesystem::path(dir) / "rg";
                if (::access(candidate.c_str(), X_OK) == 0) {
                    return candidate;
                }
            }
            if (sep == std::string_view::npos) {
                break;
            }
            start = sep + 1;
        }
        return std::nullopt;
    }

    // Spawns rgPath with argv (posix_spawn, not fork -- same "avoid full
    // address-space duplication" preference Lsp/Transport.cpp's own header
    // comment documents), redirects the child's stdin from /dev/null (rg
    // never reads it) and stderr to /dev/null (this is a live TUI app -- a
    // child writing to an inherited stderr fd would corrupt the terminal
    // display), captures stdout via a pipe read to EOF, and waits for exit.
    // Returns nullopt on any failure (spawn error, a real error exit code)
    // -- the caller falls back to the builtin scanner in every such case.
    std::optional<std::string> RunCapturingStdout(const std::filesystem::path& executable, const std::vector<std::string>& args) {
        int pipeFds[2];
        if (::pipe(pipeFds) != 0) {
            return std::nullopt;
        }

        posix_spawn_file_actions_t fileActions;
        posix_spawn_file_actions_init(&fileActions);
        posix_spawn_file_actions_addopen(&fileActions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
        posix_spawn_file_actions_adddup2(&fileActions, pipeFds[1], STDOUT_FILENO);
        posix_spawn_file_actions_addclose(&fileActions, pipeFds[0]);
        posix_spawn_file_actions_addclose(&fileActions, pipeFds[1]);
        posix_spawn_file_actions_addopen(&fileActions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);

        std::vector<char*> argv;
        argv.reserve(args.size() + 2);
        argv.push_back(const_cast<char*>(executable.c_str()));
        for (const std::string& arg : args) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);

        pid_t     childPid    = -1;
        const int spawnResult = posix_spawn(&childPid, executable.c_str(), &fileActions, nullptr, argv.data(), environ);
        posix_spawn_file_actions_destroy(&fileActions);
        ::close(pipeFds[1]);

        if (spawnResult != 0) {
            ::close(pipeFds[0]);
            return std::nullopt;
        }

        std::string    output;
        char           buffer[65536];
        ssize_t        n;
        while ((n = ::read(pipeFds[0], buffer, sizeof(buffer))) > 0) {
            output.append(buffer, static_cast<std::size_t>(n));
        }
        ::close(pipeFds[0]);

        int status = 0;
        ::waitpid(childPid, &status, 0);
        // rg's own exit codes: 0 = at least one match, 1 = ran fine, no
        // matches (not an error), 2 = a real error (bad pattern, i/o
        // failure, ...).
        if (WIFEXITED(status) && (WEXITSTATUS(status) == 0 || WEXITSTATUS(status) == 1)) {
            return output;
        }
        return std::nullopt;
    }

    // Parses ripgrep's own --json output (newline-delimited JSON, one
    // object per line) into SearchMatch, keeping only "match" events.
    std::vector<SearchMatch> ParseRipgrepJson(const std::string& output) {
        std::vector<SearchMatch> matches;

        std::size_t pos = 0;
        while (pos < output.size()) {
            const std::size_t      newline = output.find('\n', pos);
            const std::string_view lineView =
                std::string_view(output).substr(pos, newline == std::string::npos ? std::string::npos : newline - pos);
            pos = (newline == std::string::npos) ? output.size() : newline + 1;
            if (lineView.empty()) {
                continue;
            }

            Json event;
            try {
                event = Json::parse(lineView);
            }
            catch (const Json::parse_error&) {
                continue; // a malformed/unexpected line -- skip rather than fail the whole search
            }
            if (event.value("type", std::string()) != "match") {
                continue;
            }

            const Json& data = event["data"];
            std::string lineText = data["lines"]["text"].get<std::string>();
            // rg's own lines.text includes the trailing newline -- stripped
            // to match SearchWithBuiltinScanner's std::getline-based
            // (newline-stripped) convention.
            if (!lineText.empty() && lineText.back() == '\n') {
                lineText.pop_back();
            }

            matches.push_back(SearchMatch{
                std::filesystem::path(data["path"]["text"].get<std::string>()),
                data.value("line_number", static_cast<std::size_t>(0)),
                std::move(lineText),
            });
        }
        return matches;
    }

    // nullopt means "couldn't run rg at all, or it failed" -- the caller
    // falls back to the builtin scanner. A real, successful run with zero
    // matches returns an empty (non-null) vector, which the caller must
    // NOT then also re-run through the builtin scanner.
    std::optional<std::vector<SearchMatch>> SearchWithRipgrep(const std::filesystem::path& rgPath, const std::filesystem::path& absoluteRoot,
                                                               const std::string& pattern) {
        // --no-config: ignores the invoking user's own ~/.config/ripgrep/
        // config / $RIPGREP_CONFIG_PATH, so behavior stays predictable
        // regardless of whose machine this runs on, not silently altered
        // by e.g. a personal --smart-case alias. "--" before the path
        // guards against a pattern or path starting with '-' being
        // misparsed as a flag.
        const std::vector<std::string> args = {
            "--json", "--no-config", "--regexp", pattern, "--", absoluteRoot.string(),
        };

        const std::optional<std::string> output = RunCapturingStdout(rgPath, args);
        if (!output) {
            return std::nullopt;
        }
        return ParseRipgrepJson(*output);
    }

} // namespace

std::vector<SearchMatch> SearchDirectory(const std::filesystem::path& root, const std::string& pattern) {
    // Constructed first, always, regardless of which backend ends up
    // actually running -- preserves SearchDirectory's own documented
    // "throws std::regex_error for an invalid pattern" contract
    // identically either way (existing callers/tests depend on this). A
    // pattern that's valid ECMAScript but not valid in rg's own regex
    // syntax still degrades gracefully to the slower builtin path rather
    // than erroring out, since SearchWithRipgrep failing for any reason
    // triggers that same fallback below.
    const std::regex regex(pattern); // throws std::regex_error on invalid syntax

    std::error_code             ec;
    const std::filesystem::path absoluteRoot = std::filesystem::absolute(root, ec);
    if (ec) {
        return {};
    }

    if (const std::optional<std::filesystem::path> rgPath = FindRipgrepOnPath()) {
        if (std::optional<std::vector<SearchMatch>> matches = SearchWithRipgrep(*rgPath, absoluteRoot, pattern)) {
            return std::move(*matches);
        }
        // rg found on $PATH but the actual invocation failed for some
        // reason (spawn error, a real rg error exit code) -- fall through
        // to the builtin scanner rather than silently reporting zero
        // results for what might be a perfectly good search.
    }

    return SearchWithBuiltinScanner(absoluteRoot, regex);
}

} // namespace ned::editor
