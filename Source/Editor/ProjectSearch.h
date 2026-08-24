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

#ifndef NED_EDITOR_PROJECTSEARCH_H
#define NED_EDITOR_PROJECTSEARCH_H

#include <cstddef>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

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

} // namespace ned::editor

#endif // NED_EDITOR_PROJECTSEARCH_H
