//
// .editorconfig (https://editorconfig.org): per-directory settings files a
// project checks in. A file's properties come from every .editorconfig
// between it and the first one marked `root = true` (or the filesystem
// root), nearest last, each file's matching sections in order -- a later
// match overrides an earlier one, and `unset` removes a property.
//
// Only reading and matching live here; what a property means is up to its
// consumer (EditorConfigIndent below is the indentation one).
//

#ifndef NED_EDITOR_EDITORCONFIG_H
#define NED_EDITOR_EDITORCONFIG_H

#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Editor/IndentStyle.h"
#include "Text/FileConventions.h"

namespace ned::editor {

struct EditorConfigSection {
    std::string                                      pattern;
    std::vector<std::pair<std::string, std::string>> properties; // keys lowercased, values as written
};

struct EditorConfigFile {
    bool                             root = false;
    std::vector<EditorConfigSection> sections;
};

[[nodiscard]] EditorConfigFile ParseEditorConfig(std::string_view text);

// Whether a section's glob matches `relativePath` (generic '/' separators,
// relative to the directory holding the .editorconfig). `*` stays within a
// path segment, `**` crosses them, plus `?`, `[abc]`/`[!abc]`, `{a,b}` and
// `{n1..n2}` integer ranges. A pattern with no '/' matches at any depth.
[[nodiscard]] bool EditorConfigGlobMatches(std::string_view pattern, std::string_view relativePath);

// Every property that applies to `path`, keys and values lowercased.
[[nodiscard]] std::map<std::string, std::string> EditorConfigPropertiesFor(const std::filesystem::path& path);

// indent_style / indent_size / tab_width, as far as the properties state them.
[[nodiscard]] IndentOverride EditorConfigIndent(const std::map<std::string, std::string>& properties);

// end_of_line, insert_final_newline, trim_trailing_whitespace, charset and
// max_line_length ("off" is no limit).
[[nodiscard]] text::FileConventions EditorConfigConventions(const std::map<std::string, std::string>& properties);

// Makes every load decode a file in the charset .editorconfig states for it
// (text::SetStatedCharsetResolver), while .editorconfig is enabled. A file's
// own byte order mark still wins.
void InstallEditorConfigCharsetResolver();

// Process-wide toggle, ned/set-editorconfig-enabled (default true).
void               SetEditorConfigEnabled(bool enabled);
[[nodiscard]] bool EditorConfigEnabled();

} // namespace ned::editor

#endif // NED_EDITOR_EDITORCONFIG_H
