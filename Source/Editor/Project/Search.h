//
// Project-wide search (project-search follow-up): a UI-agnostic recursive
// directory search, driven by BufferView the same way find-file/
// switch-to-buffer are -- see Commands.cpp/BufferView.cpp for the
// interactive side.
//
// internal-project-search follow-up: runs entirely in-process now -- no `rg`
// shell-out. The old fallback (a single-threaded std::filesystem::
// recursive_directory_iterator + std::regex_search-per-line scanner) was the
// whole reason `rg` got adopted as the preferred backend in the first place;
// replacing both with a real engine removes the external-binary dependency
// instead of just working around its absence. Matching runs on RE2 (linear-
// time, no catastrophic backtracking -- the same engine-model philosophy
// `rg` itself is built on) across a small worker-thread pool (see
// SearchSettings.h's ProjectSearchThreads -- default 4; this is I/O-bound,
// not CPU-bound, so more threads than that mostly just contends on the same
// disk/page cache). Directory walking (dot-directories, GitIgnore.h's
// .gitignore matcher, and Text/BinaryDetect.h's binary-file sniff) stays
// single-threaded -- only the per-file line scan is parallelized -- with
// results reassembled back into the original file-visitation order
// regardless of which worker actually processed a given file, so this
// function's output is deterministic and independent of thread scheduling.
//

#ifndef NED_EDITOR_PROJECT_SEARCH_H
#define NED_EDITOR_PROJECT_SEARCH_H

#include <cstddef>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace ned::text {
class BufferList;
} // namespace ned::text

namespace ned::editor {

struct SearchMatch {
    std::filesystem::path file;       // always absolute, regardless of root's form
    std::size_t           lineNumber; // 1-indexed
    std::string           lineText;
};

// internal-project-search follow-up: RE2 has no exception-based error API --
// a bad pattern just leaves the constructed RE2 in a not-ok() state with its
// own diagnostic string (RE2::error()) -- so this is SearchDirectory's
// replacement for the old std::regex_error, carrying that diagnostic through
// what() so every "Invalid regex: " + e.what() call site keeps working
// unchanged in shape, just against a real message describing what RE2
// actually rejected rather than std::regex's own.
class SearchPatternError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

// Recursively searches every regular text file under root for pattern (RE2
// syntax -- see https://github.com/google/re2/wiki/Syntax; close to but not
// identical to std::regex's ECMAScript grammar, most notably: no
// backreferences, no lookaround), returning one SearchMatch per matching
// line, in the order files are visited then top-to-bottom within each file.
// Throws SearchPatternError on invalid pattern syntax. Returns an empty list
// rather than throwing if root doesn't exist or can't be listed.
[[nodiscard]] std::vector<SearchMatch> SearchDirectory(const std::filesystem::path& root, const std::string& pattern);

// live-buffer-search follow-up: the same search, but an open buffer's own
// content is what gets searched instead of its file. Editing a file and then
// not finding what was just typed is the editor lying about its own state --
// the buffer is the truth the user is working against, the file is the thing
// about to be overwritten.
//
// Only *modified* buffers are snapshotted: an unmodified one is byte-identical
// to its file, so reading the file is both correct and cheaper. A huge buffer
// (ITextStorage::IsHuge) is deliberately left to the disk read too rather than
// materialized whole just to be searched -- the same bound every other
// huge-file path in this codebase keeps. A modified buffer whose file doesn't
// exist on disk yet (a never-saved new file) is searched anyway, appended
// after the walk's own files in path order, so results stay deterministic;
// .gitignore and the dot-directory rule apply to it exactly as they would if
// it were on disk.
//
// The snapshot is taken on the calling thread before any worker starts, so
// BufferList (main-thread-only, like every other Text/ type) is never touched
// concurrently.
[[nodiscard]] std::vector<SearchMatch> SearchDirectory(const std::filesystem::path& root, const std::string& pattern,
                                                       text::BufferList& liveBuffers);

// multibuffer-search-in-results follow-up: the same per-file scan, over an
// explicit file list instead of a directory walk -- what "search within
// these results" narrows a fresh search down to (the set of files an
// existing multibuffer/results buffer references). No .gitignore or
// dot-directory filtering and no binary sniff: the caller already decided
// which files are interesting, and second-guessing that here would silently
// drop results from a file the user is plainly looking at.
//
// Live-buffer semantics are the SearchDirectory(root, pattern, liveBuffers)
// overload's, verbatim: a modified open buffer is searched in place of its
// file (line-chunked through its own storage when huge), an unmodified one
// is byte-identical to its file and read from disk. Duplicate and
// nonexistent paths are dropped; order follows the caller's list.
[[nodiscard]] std::vector<SearchMatch> SearchFiles(const std::vector<std::filesystem::path>& files,
                                                   const std::string& pattern, text::BufferList& liveBuffers);

} // namespace ned::editor

#endif // NED_EDITOR_PROJECT_SEARCH_H
