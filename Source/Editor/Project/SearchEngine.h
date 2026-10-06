//
// Project-wide text search over files on disk, with no editor state: what a
// headless host runs. Search.h layers open buffers' unsaved edits on top as
// LiveContent.
//
// Matching runs on RE2 (linear time, no backtracking blowup) across a small
// worker pool (SearchOptions::threads). The directory walk -- dot-directories,
// GitIgnore.h's matcher, Text/BinaryDetect.h's sniff -- runs on the calling
// thread, feeding the pool as it finds files, then joins the scan once it's
// done. Results come back in walk order regardless of which worker scanned a
// file.
//

#ifndef NED_EDITOR_PROJECT_SEARCHENGINE_H
#define NED_EDITOR_PROJECT_SEARCHENGINE_H

#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace re2 {
class RE2;
} // namespace re2

namespace ned::editor {

struct SearchMatch {
    std::filesystem::path file;         // always absolute, regardless of root's form
    std::size_t           lineNumber{}; // 1-indexed
    std::string           lineText;
};

// RE2 rejects a pattern by state rather than by throwing; this carries its
// diagnostic through what().
class SearchPatternError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

// A compiled pattern in RE2 syntax (https://github.com/google/re2/wiki/Syntax):
// close to ECMAScript, but no backreferences or lookaround. Safe to share
// across threads.
class SearchPattern {
  public:
    // Throws SearchPatternError on invalid syntax.
    explicit SearchPattern(const std::string& pattern);
    ~SearchPattern();

    SearchPattern(SearchPattern&&) noexcept;
    SearchPattern& operator=(SearchPattern&&) noexcept;

    [[nodiscard]] bool Matches(std::string_view line) const;

  private:
    std::unique_ptr<re2::RE2> regex_;
};

// Content searched in place of a file on disk -- an open buffer's unsaved
// edits. It may stand for a file that doesn't exist yet.
struct LiveContent {
    std::filesystem::path path;          // absolute and lexically normal
    std::filesystem::path canonicalPath; // weakly canonical, so a symlinked spelling still matches
    std::string           text;          // searched unless preScanned is set
    // Matches the caller already found, for content too large to hand over
    // as one string.
    std::optional<std::vector<SearchMatch>> preScanned;
};

// A search root in each spelling live content can be compared against: a
// buffer opened through the real path still belongs to a root reached
// through a symlink.
class SearchRoot {
  public:
    explicit SearchRoot(const std::filesystem::path& root);

    // False if root can't be made absolute; nothing is under it then.
    [[nodiscard]] bool Valid() const noexcept {
        return !normalized_.empty();
    }

    // `path` relative to the root, through whichever spelling it's under, or
    // nullopt if it's under neither.
    [[nodiscard]] std::optional<std::filesystem::path> Relative(const std::filesystem::path& path,
                                                                const std::filesystem::path& canonicalPath) const;

    [[nodiscard]] const std::filesystem::path& Normalized() const noexcept {
        return normalized_;
    }
    [[nodiscard]] const std::filesystem::path& Canonical() const noexcept {
        return canonical_;
    }

  private:
    std::filesystem::path normalized_;
    std::filesystem::path canonical_;
};

// The settings a search runs under, explicit so a host can run the
// editor's rather than its own.
struct SearchOptions {
    unsigned int threads      = 4;    // scan workers, capped by the machine's cores
    bool         editorConfig = true; // decode a file in the charset .editorconfig states
};

// The process-wide settings: ProjectSearchThreads, EditorConfigEnabled.
[[nodiscard]] SearchOptions CurrentSearchOptions();

// Receives a streamed search's matches as runs of finished files, in the
// order SearchDirectory would return them. Called on a search thread, never
// concurrently and never with an empty run. Must not throw.
using SearchSink = std::function<void(std::vector<SearchMatch> matches)>;

// Recursively searches every regular text file under root, returning one
// SearchMatch per matching line, in walk order then top to bottom. Throws
// SearchPatternError on invalid syntax. Returns an empty list if root can't
// be listed, or once stop is requested (checked between files).
[[nodiscard]] std::vector<SearchMatch> SearchDirectory(const std::filesystem::path& root, const std::string& pattern,
                                                       const std::stop_token& stop = {});

// As above, with each `live` entry searched in place of its file. Entries the
// walk never visits (a file not yet on disk) are appended in path order,
// subject to the same dot-directory and .gitignore rules. An entry counts as
// under root by SearchRoot's rule and is reported under root's spelling;
// entries outside it are ignored.
[[nodiscard]] std::vector<SearchMatch> SearchDirectory(const std::filesystem::path& root, const SearchPattern& pattern,
                                                       std::span<const LiveContent> live,
                                                       const std::stop_token&       stop = {});

// SearchDirectory, streamed: matches reach onMatches as files finish
// rather than after the whole walk. Nothing more is sent once stop is
// requested.
void StreamSearchDirectory(const std::filesystem::path& root, const SearchPattern& pattern,
                           std::span<const LiveContent> live, const SearchOptions& options,
                           const SearchSink& onMatches, const std::stop_token& stop = {});

// The same per-file scan over an explicit file list instead of a walk. No
// .gitignore, dot-directory or binary filtering: the caller already chose
// these files. Duplicates and paths with neither a file nor a `live` entry
// are dropped; order follows `files`.
[[nodiscard]] std::vector<SearchMatch> SearchFiles(const std::vector<std::filesystem::path>& files,
                                                   const SearchPattern& pattern, std::span<const LiveContent> live);

} // namespace ned::editor

#endif // NED_EDITOR_PROJECT_SEARCHENGINE_H
