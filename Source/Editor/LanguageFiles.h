//
// A language's files -- its query data (Editor/QueryData.h) and its
// `language.janet` -- addressed by path and read from one of two places: the
// bundled data tree (a path relative to `DataDir()/languages`,
// "cpp/highlights.janet") or the filesystem (an absolute path, a user's or a
// project's own language directory). One reader for both, so a bundled
// language and a runtime-loaded one go through exactly the same code.
//
// CompileQueryFiles is the step between a query file and the matcher: the
// Janet-syntax data read, then emitted as query text, cached per path. Its
// result remembers which file each byte came from, so a compile error
// -- which is a byte offset into the concatenated text -- can be reported as
// `path:line`.
//

#ifndef NED_EDITOR_LANGUAGEFILES_H
#define NED_EDITOR_LANGUAGEFILES_H

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace ned::editor {

// DataDir()/languages -- the root every relative language path counts from.
[[nodiscard]] const std::filesystem::path& BundledLanguagesRoot();

struct BundledLanguageFile {
    std::string path; // relative to BundledLanguagesRoot(), e.g. "cpp/highlights.janet"
    std::string content;
};

// Every *.janet under the bundled root, sorted by path.
[[nodiscard]] std::vector<BundledLanguageFile> BundledLanguageFiles();

[[nodiscard]] bool BundledLanguageFileExists(std::string_view path);

// Bundled (relative path) or filesystem (absolute path). Throws
// std::runtime_error naming the path when neither has it.
[[nodiscard]] std::string ReadLanguageFile(std::string_view path);

struct QueryText {
    struct Segment {
        std::string path;
        std::size_t offset; // where this file's text begins in `text`
    };
    std::string          text;
    std::vector<Segment> segments;

    // "path:line" for a byte offset into `text`.
    [[nodiscard]] std::string Locate(std::size_t offset) const;
};

// Reads, parses and emits each file (cached per path -- a Mode is rebuilt
// per lookup, the parse is not), concatenated in order. A `.scm` path is
// read in tree-sitter's own syntax, for a runtime-loaded grammar whose
// queries came straight from a system install. Throws std::runtime_error
// with `path:line:column` for a malformed file.
[[nodiscard]] QueryText CompileQueryFiles(const std::vector<std::string>& paths);

} // namespace ned::editor

#endif // NED_EDITOR_LANGUAGEFILES_H
