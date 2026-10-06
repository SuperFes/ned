//
// Project-wide text search over files on disk, with no editor state: what a
// headless host runs. Search.h layers open buffers' unsaved edits on top as
// LiveContent.
//
// Matching runs on RE2 (linear time, no backtracking blowup) across a small
// worker pool (SearchSettings.h's ProjectSearchThreads). The directory walk
// -- dot-directories, GitIgnore.h's matcher, Text/BinaryDetect.h's sniff --
// stays on the calling thread; only the per-file line scan is parallel, and
// results come back in walk order regardless of which worker scanned a file.
//

#ifndef NED_EDITOR_PROJECT_SEARCHENGINE_H
#define NED_EDITOR_PROJECT_SEARCHENGINE_H

#include <cstddef>
#include <filesystem>
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
    std::filesystem::path path;          // absolute and lexically normal; reported as SearchMatch::file
    std::filesystem::path canonicalPath; // weakly canonical, so a symlinked spelling still matches
    std::string           text;          // searched unless preScanned is set
    // Matches the caller already found, for content too large to hand over
    // as one string.
    std::optional<std::vector<SearchMatch>> preScanned;
};

// Recursively searches every regular text file under root, returning one
// SearchMatch per matching line, in walk order then top to bottom. Throws
// SearchPatternError on invalid syntax. Returns an empty list if root can't
// be listed, or once stop is requested (checked between files).
[[nodiscard]] std::vector<SearchMatch> SearchDirectory(const std::filesystem::path& root, const std::string& pattern,
                                                       const std::stop_token& stop = {});

// As above, with each `live` entry searched in place of its file. Entries the
// walk never visits (a file not yet on disk) are appended in path order,
// subject to the same dot-directory and .gitignore rules; entries outside
// root are ignored.
[[nodiscard]] std::vector<SearchMatch> SearchDirectory(const std::filesystem::path& root, const SearchPattern& pattern,
                                                       std::span<const LiveContent> live,
                                                       const std::stop_token&       stop = {});

// The same per-file scan over an explicit file list instead of a walk. No
// .gitignore, dot-directory or binary filtering: the caller already chose
// these files. Duplicates and paths with neither a file nor a `live` entry
// are dropped; order follows `files`.
[[nodiscard]] std::vector<SearchMatch> SearchFiles(const std::vector<std::filesystem::path>& files,
                                                   const SearchPattern& pattern, std::span<const LiveContent> live);

} // namespace ned::editor

#endif // NED_EDITOR_PROJECT_SEARCHENGINE_H
