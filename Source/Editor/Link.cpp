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

    // [start, end) byte range of point's own "statement" within its line --
    // the nearest ';'/',' strictly before/after point, or the line's own
    // bounds if neither appears. This is what lets a quoted/bracketed
    // target be found anywhere in "#include <vector>" without point having
    // to land on "<vector>" itself, while still not reaching across into an
    // unrelated statement on the same line (e.g. "foo(); #include <bar>").
    std::pair<std::size_t, std::size_t> StatementSegmentBounds(std::string_view bufferText, std::size_t lineStart,
                                                               std::size_t lineEnd, std::size_t point) {
        std::size_t segStart = point;
        while (segStart > lineStart && bufferText[segStart - 1] != ';' && bufferText[segStart - 1] != ',') {
            --segStart;
        }
        std::size_t segEnd = point;
        while (segEnd < lineEnd && bufferText[segEnd] != ';' && bufferText[segEnd] != ',') {
            ++segEnd;
        }
        return {segStart, segEnd};
    }

    // True if line (leading whitespace trimmed) starts with "#include" --
    // the one C-family construct where bare angle brackets denote a file
    // path, as opposed to template syntax ("vector<int>") that happens to
    // share the same delimiter characters.
    bool LineIsPreprocessorInclude(std::string_view line) {
        std::size_t i = 0;
        while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) {
            ++i;
        }
        return line.substr(i).rfind("#include", 0) == 0;
    }

    // One quote- or angle-bracket-delimited run found while scanning a
    // statement segment for a link-shaped target. Absolute offsets into the
    // same bufferText DetectLinkAtPoint was called with, so callers can
    // compare directly against point; [start, end) spans the delimiters
    // themselves, not just their contents.
    struct DelimitedRun {
        std::size_t start;
        std::size_t end;
    };

    // Finds every "..."/'...' run in [segStart, segEnd), plus every <...>
    // run too when includeAngle is set (LineIsPreprocessorInclude gates
    // that from the caller) -- one pass, matching pairs of the same
    // delimiter on the same line; not nested/escaped-quote-aware, since a
    // real #include/import target never needs either.
    std::vector<DelimitedRun> FindDelimitedRuns(std::string_view bufferText, std::size_t segStart, std::size_t segEnd,
                                                bool includeAngle) {
        std::vector<DelimitedRun> runs;
        std::size_t                i = segStart;
        while (i < segEnd) {
            const char c       = bufferText[i];
            char       closing = 0;
            if (c == '"' || c == '\'') {
                closing = c;
            }
            else if (c == '<' && includeAngle) {
                closing = '>';
            }
            if (closing != 0) {
                const std::size_t closeIndex = bufferText.find(closing, i + 1);
                if (closeIndex != std::string_view::npos && closeIndex < segEnd) {
                    runs.push_back({i, closeIndex + 1});
                    i = closeIndex + 1;
                    continue;
                }
            }
            ++i;
        }
        return runs;
    }

} // namespace

std::string_view StripDelimiters(std::string_view token) {
    if (token.size() >= 2) {
        const char first = token.front();
        const char last  = token.back();
        if ((first == '"' && last == '"') || (first == '\'' && last == '\'') || (first == '<' && last == '>')) {
            return token.substr(1, token.size() - 2);
        }
    }
    return token;
}

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

    // 2. Whitespace-delimited token under point, if path-shaped. Tried
    // before the broadened, statement-wide step 3 below -- an exact match
    // right under the cursor always wins over a guess from surrounding
    // context, so step 3 only ever runs when point isn't sitting on a
    // usable candidate of its own.
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
        const std::string_view rawToken   = bufferText.substr(tokenStart, tokenEnd - tokenStart);
        const std::string_view stripped   = StripDelimiters(rawToken);
        const bool              wasDelimited = stripped.size() != rawToken.size();
        if (wasDelimited) {
            tokenStart = static_cast<std::size_t>(stripped.data() - bufferText.data());
            tokenEnd   = tokenStart + stripped.size();
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

    // 3. Fallback: a quoted/angle-bracketed target anywhere within point's
    // own statement, reached only when step 2 found nothing right under
    // the cursor -- see this function's own doc comment in Link.h for the
    // full reasoning (why point doesn't need to land on the target's own
    // bytes here, and why angle brackets are gated on "#include").
    {
        const auto [segStart, segEnd] = StatementSegmentBounds(bufferText, lineStart, lineEnd, point);
        const std::vector<DelimitedRun> runs = FindDelimitedRuns(bufferText, segStart, segEnd, LineIsPreprocessorInclude(line));

        const DelimitedRun* best         = nullptr;
        std::size_t         bestDistance = std::string_view::npos;
        for (const DelimitedRun& run : runs) {
            const std::size_t distance = (point < run.start) ? run.start - point : (point > run.end ? point - run.end : 0);
            if (distance < bestDistance) {
                bestDistance = distance;
                best         = &run;
            }
        }
        if (best && best->end - best->start >= 2) {
            const std::size_t targetStart = best->start + 1;
            const std::size_t targetEnd   = best->end - 1;
            if (targetEnd > targetStart) {
                return DetectedLink{
                    .kind      = LinkKind::File,
                    .target    = std::string(bufferText.substr(targetStart, targetEnd - targetStart)),
                    .startByte = targetStart,
                    .endByte   = targetEnd,
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

namespace {

    // import-target-tree-sitter follow-up: tries candidate as-is, then
    // candidate with each of candidateExtensions appended, then
    // candidate/basename.extension for each (basename, extension) pair in
    // indexBasenames x candidateExtensions -- the package/directory-import
    // shape (a JS "./foo" resolving via "./foo/index.js", a Python "foo.bar"
    // resolving via "foo/bar/__init__.py"). With both lists empty this is
    // exactly the single exists() check ResolveFileLink always did. When
    // candidate exists but is itself a directory and indexBasenames is
    // non-empty, the exact match is deliberately skipped in favor of an
    // index-file match inside it -- a bare directory isn't something a text
    // buffer can open, so "foo" resolving to a real directory "foo/" is only
    // useful once it's actually resolved on down to "foo/index.js".
    std::optional<std::filesystem::path> TryVariants(const std::filesystem::path&    candidate,
                                                      const std::vector<std::string>& candidateExtensions,
                                                      const std::vector<std::string>& indexBasenames) {
        const bool preferIndexOverDirectory = !indexBasenames.empty() && std::filesystem::is_directory(candidate);
        if (!preferIndexOverDirectory && std::filesystem::exists(candidate)) {
            return candidate;
        }
        for (const std::string& extension : candidateExtensions) {
            std::filesystem::path withExtension = candidate;
            withExtension += ("." + extension);
            if (std::filesystem::exists(withExtension)) {
                return withExtension;
            }
        }
        for (const std::string& basename : indexBasenames) {
            for (const std::string& extension : candidateExtensions) {
                if (const std::filesystem::path indexFile = candidate / (basename + "." + extension);
                    std::filesystem::exists(indexFile)) {
                    return indexFile;
                }
            }
        }
        return std::nullopt;
    }

} // namespace

std::optional<std::filesystem::path> ResolveFileLink(const std::string& target, const std::filesystem::path& baseDirectory,
                                                     const std::vector<std::filesystem::path>& includePaths,
                                                     const std::vector<std::string>&           candidateExtensions,
                                                     const std::vector<std::string>&           indexBasenames) {
    const std::filesystem::path targetPath(target);

    if (targetPath.is_absolute()) {
        return TryVariants(targetPath, candidateExtensions, indexBasenames);
    }

    if (const auto resolved = TryVariants(baseDirectory / targetPath, candidateExtensions, indexBasenames)) {
        return resolved;
    }
    if (const auto resolved = TryVariants(ProjectRoot() / targetPath, candidateExtensions, indexBasenames)) {
        return resolved;
    }
    for (const std::filesystem::path& includePath : includePaths) {
        if (const auto resolved = TryVariants(includePath / targetPath, candidateExtensions, indexBasenames)) {
            return resolved;
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
