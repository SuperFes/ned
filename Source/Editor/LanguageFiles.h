//
// A language's files -- its query data (Editor/QueryData.h) and, soon, its
// `language.janet` -- addressed by path and read from one of two places: the
// table compiled into the binary from `Source/Languages/` (a relative path,
// "cpp/highlights.janet"), or the filesystem (an absolute path, a user's or a
// project's own language directory). One reader for both, so a bundled
// language and a runtime-loaded one go through exactly the same code.
//
// CompileQueryFiles is the step between a query file and tree-sitter: the
// Janet-syntax data read, then emitted as query text, cached per path. Its
// result remembers which file each byte came from, so a tree-sitter error
// -- which is a byte offset into the concatenated text -- can be reported as
// `path:line`.
//

#ifndef NED_EDITOR_LANGUAGEFILES_H
#define NED_EDITOR_LANGUAGEFILES_H

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ned::editor {

struct EmbeddedLanguageFile {
    std::string_view path; // relative to Source/Languages, e.g. "cpp/highlights.janet"
    std::string_view content;
};

// Generated at configure time by CMake's ned_embed_language_files from every
// *.janet under Source/Languages -- see CMakeLists.txt.
[[nodiscard]] std::span<const EmbeddedLanguageFile> EmbeddedLanguageFiles();

[[nodiscard]] std::optional<std::string_view> FindEmbeddedLanguageFile(std::string_view path);

// Embedded (relative path) or filesystem (absolute path). Throws
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
