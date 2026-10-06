//
// Project-wide search as the editor sees it: SearchEngine.h's disk search,
// with each open buffer's unsaved edits searched in place of its file.
// Editing a file and then not finding what was just typed would be the
// editor misreporting its own state. The interactive side is driven from
// BufferView, the same way find-file and switch-to-buffer are.
//

#ifndef NED_EDITOR_PROJECT_SEARCH_H
#define NED_EDITOR_PROJECT_SEARCH_H

#include <filesystem>
#include <string>
#include <vector>

#include "Editor/Project/SearchEngine.h"

namespace ned::text {
class BufferList;
} // namespace ned::text

namespace ned::editor {

// SearchDirectory with live buffers. Only *modified* buffers are snapshotted:
// an unmodified one is byte-identical to its file, so reading the file is
// both correct and cheaper. A huge buffer (ITextStorage::IsHuge) is never
// materialized whole; it's scanned line-chunk by line-chunk through its own
// storage instead. A modified buffer whose file doesn't exist yet is searched
// anyway, under the walk's own .gitignore and dot-directory rules.
//
// The snapshot is taken on the calling thread before any worker starts, so
// BufferList (main-thread-only, like every other Text/ type) is never touched
// concurrently.
[[nodiscard]] std::vector<SearchMatch> SearchDirectory(const std::filesystem::path& root, const std::string& pattern,
                                                       text::BufferList& liveBuffers);

// SearchFiles with the same live-buffer rules -- what "search within these
// results" narrows a fresh search down to.
[[nodiscard]] std::vector<SearchMatch> SearchFiles(const std::vector<std::filesystem::path>& files,
                                                   const std::string& pattern, text::BufferList& liveBuffers);

} // namespace ned::editor

#endif // NED_EDITOR_PROJECT_SEARCH_H
