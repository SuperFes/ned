#include "Link.h"

#include <algorithm>
#include <cctype>
#include <mutex>
#include <regex>
#include <thread>
#include <utility>

#include <sys/wait.h>
#include <unistd.h>

#include "ProjectRoot.h"

namespace ned::editor::link {

namespace {

    std::mutex& CommandMutex() {
        static std::mutex mutex;
        return mutex;
    }

    std::optional<std::string>& CommandStorage() {
        static std::optional<std::string> command = "xdg-open";
        return command;
    }

    // "https?://" followed by one or more non-whitespace bytes -- matches
    // this codebase's established std::regex/ECMAScript convention
    // (QueryReplace, ProjectSearch, Org.h) over hand-rolled scanning.
    const std::regex& UrlPattern() {
        static const std::regex pattern(R"(https?://\S+)");
        return pattern;
    }

    // Strips exactly one trailing punctuation character, if present, so a
    // URL at the end of a sentence ("see https://example.com.") doesn't
    // swallow its own period. Deliberately not a balanced-paren-aware
    // linkifier -- a real one would also need to tolerate a matching '('
    // earlier in the URL (e.g. Wikipedia URLs); out of scope for v1.
    std::string_view TrimTrailingPunctuation(std::string_view url) {
        if (!url.empty() && std::string_view(")]}>.,;:!?'\"").find(url.back()) != std::string_view::npos) {
            url.remove_suffix(1);
        }
        return url;
    }

    // A candidate File token "looks path-shaped" only if it contains a '/'
    // or has a '.'-plus-suffix that reads as a file extension -- a bare word
    // like "TODO" is never classified as a File candidate at all. See
    // DetectLinkAtPoint's own doc comment in Link.h for why this matters.
    bool LooksPathShaped(std::string_view token) {
        if (token.find('/') != std::string_view::npos) {
            return true;
        }
        const std::size_t dot = token.rfind('.');
        return dot != std::string_view::npos && dot > 0 && dot + 1 < token.size();
    }

    // [start, end) byte range of the line containing point.
    std::pair<std::size_t, std::size_t> LineBoundsAtPoint(std::string_view bufferText, std::size_t point) {
        point                       = std::min(point, bufferText.size());
        const std::size_t start     = bufferText.rfind('\n', point == 0 ? 0 : point - 1);
        const std::size_t lineStart = (start == std::string_view::npos) ? 0 : start + 1;
        const std::size_t end       = bufferText.find('\n', point);
        const std::size_t lineEnd   = (end == std::string_view::npos) ? bufferText.size() : end;
        return {lineStart, lineEnd};
    }

} // namespace

std::optional<DetectedLink> DetectLinkAtPoint(std::string_view bufferText, std::size_t point) {
    const auto [lineStart, lineEnd] = LineBoundsAtPoint(bufferText, point);
    const std::string_view line     = bufferText.substr(lineStart, lineEnd - lineStart);

    // 1. Bare URL.
    {
        const std::cregex_iterator end;
        for (std::cregex_iterator it(line.data(), line.data() + line.size(), UrlPattern()); it != end; ++it) {
            const auto&       match      = *it;
            const std::size_t matchStart = lineStart + static_cast<std::size_t>(match.position());
            std::string_view  matched(line.data() + match.position(), static_cast<std::size_t>(match.length()));
            matched                    = TrimTrailingPunctuation(matched);
            const std::size_t matchEnd = matchStart + matched.size();
            if (point >= matchStart && point <= matchEnd) {
                return DetectedLink{
                    .kind      = LinkKind::Url,
                    .target    = std::string(matched),
                    .startByte = matchStart,
                    .endByte   = matchEnd,
                };
            }
        }
    }

    // 2. Whitespace-delimited token under point, if path-shaped.
    {
        std::size_t tokenStart = point;
        while (tokenStart > lineStart && !std::isspace(static_cast<unsigned char>(bufferText[tokenStart - 1]))) {
            --tokenStart;
        }
        std::size_t tokenEnd = point;
        while (tokenEnd < lineEnd && !std::isspace(static_cast<unsigned char>(bufferText[tokenEnd]))) {
            ++tokenEnd;
        }
        // Strip one layer of surrounding quote/angle-bracket delimiters --
        // e.g. #include "foo.h"/<foo.h>, or a quoted JS/Python import -- so
        // the delimiters themselves don't end up as part of the path handed
        // to ResolveFileLink (which would then look for a file literally
        // named "foo.h", quotes included, and never find it). An explicitly
        // delimited token (unlike a bare word) is already unambiguously a
        // quoted/bracketed reference, so it skips the extension-or-slash
        // LooksPathShaped test below -- that heuristic exists only to keep a
        // plain word like "TODO" from resolving, which doesn't apply here
        // (a delimited, extensionless case like <vector> is exactly what
        // this is meant to catch).
        bool wasDelimited = false;
        if (tokenEnd - tokenStart >= 2) {
            const char first = bufferText[tokenStart];
            const char last  = bufferText[tokenEnd - 1];
            if ((first == '"' && last == '"') || (first == '\'' && last == '\'') ||
                (first == '<' && last == '>')) {
                ++tokenStart;
                --tokenEnd;
                wasDelimited = true;
            }
        }
        if (tokenEnd > tokenStart) {
            const std::string_view token = bufferText.substr(tokenStart, tokenEnd - tokenStart);
            if (wasDelimited || LooksPathShaped(token)) {
                return DetectedLink{
                    .kind      = LinkKind::File,
                    .target    = std::string(token),
                    .startByte = tokenStart,
                    .endByte   = tokenEnd,
                };
            }
        }
    }

    return std::nullopt;
}

LinkKind ClassifyTarget(std::string_view target) {
    if (target.rfind("http://", 0) == 0 || target.rfind("https://", 0) == 0) {
        return LinkKind::Url;
    }
    return LinkKind::File;
}

std::optional<std::filesystem::path> ResolveFileLink(const std::string& target, const std::filesystem::path& baseDirectory,
                                                     const std::vector<std::filesystem::path>& includePaths) {
    const std::filesystem::path targetPath(target);

    if (targetPath.is_absolute()) {
        return std::filesystem::exists(targetPath) ? std::optional(targetPath) : std::nullopt;
    }

    if (const std::filesystem::path relativeToBase = baseDirectory / targetPath; std::filesystem::exists(relativeToBase)) {
        return relativeToBase;
    }
    if (const std::filesystem::path relativeToRoot = ProjectRoot() / targetPath; std::filesystem::exists(relativeToRoot)) {
        return relativeToRoot;
    }
    for (const std::filesystem::path& includePath : includePaths) {
        if (const std::filesystem::path candidate = includePath / targetPath; std::filesystem::exists(candidate)) {
            return candidate;
        }
    }
    return std::nullopt;
}

void SetUrlOpenCommand(std::optional<std::string> command) {
    const std::lock_guard<std::mutex> lock(CommandMutex());
    CommandStorage() = std::move(command);
}

std::optional<std::string> UrlOpenCommand() {
    const std::lock_guard<std::mutex> lock(CommandMutex());
    return CommandStorage();
}

bool OpenUrl(const std::string& url) {
    const std::optional<std::string> command = UrlOpenCommand();
    if (!command || command->empty()) {
        return false;
    }

    const pid_t pid = ::fork();
    if (pid < 0) {
        return false;
    }
    if (pid == 0) {
        // Child: detach from the terminal session so the opened application
        // (a GUI browser, most commonly) isn't tied to this process's own
        // controlling terminal, then exec -- url is passed as its own argv
        // element, never through a shell, see OpenUrl's own doc comment in
        // Link.h for why that's load-bearing, not stylistic.
        ::setsid();
        ::execlp(command->c_str(), command->c_str(), url.c_str(), static_cast<char*>(nullptr));
        ::_exit(127); // exec failed
    }

    // Reaped by a detached thread rather than a process-wide
    // signal(SIGCHLD, SIG_IGN) -- see Link.h's own doc comment for why that
    // global approach would silently break RunFormatCommand's own
    // std::system-based child-reaping elsewhere in this process.
    std::thread([pid] {
        int status = 0;
        ::waitpid(pid, &status, 0);
    }).detach();

    return true;
}

} // namespace ned::editor::link
