//
// Project-wide search (project-search follow-up): a UI-agnostic recursive
// directory search, driven by BufferView the same way find-file/
// switch-to-buffer are -- see Commands.cpp/BufferView.cpp for the
// interactive side. Read-only; there is deliberately no project-wide
// replace yet (a project-wide rewrite of many files at once, with no
// per-match confirmation and no undo across files, is a meaningfully
// higher-risk feature than a read-only search and was scoped out of this
// pass on purpose, not an oversight -- see ROADMAP.md).
//

#ifndef NED_EDITOR_PROJECTSEARCH_H
#define NED_EDITOR_PROJECTSEARCH_H

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace ned::editor {

struct SearchMatch {
    std::filesystem::path file;       // always absolute, regardless of root's form
    std::size_t           lineNumber; // 1-indexed
    std::string           lineText;
};

// Recursively searches every regular text file under root for pattern
// (std::regex, ECMAScript syntax, matching QueryReplace's own choice),
// returning one SearchMatch per matching line, in the order files are
// visited then top-to-bottom within each file. Throws std::regex_error on
// invalid pattern syntax -- the same convention QueryReplace already
// established, so callers already know how to report it ("Invalid regex:
// ...").
//
// Skips dot-directories (.git, .svn, .idea, ...) entirely -- the same
// default most search tools (ripgrep included) apply -- and skips any file
// whose first 8KiB contain a NUL byte, a standard, cheap binary-file
// heuristic (the same one git/grep use), so a match inside a compiled
// binary never shows up as unreadable garbage in the results. Returns an
// empty list rather than throwing if root doesn't exist or can't be listed.
[[nodiscard]] std::vector<SearchMatch> SearchDirectory(const std::filesystem::path& root, const std::string& pattern);

} // namespace ned::editor

#endif // NED_EDITOR_PROJECTSEARCH_H
