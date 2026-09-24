//
// Settings a buffer takes from the file it shows, applied when the file is
// opened and again whenever the file changes under the buffer. Indentation:
// what its content already does (IndentDetection), then its .editorconfig
// (EditorConfigEnabled), then -- for a Rust file -- its rustfmt.toml
// (Editor/RustfmtConfig.h), then a Vim/Emacs modeline, each overriding the
// one before field by field; whatever none of them states stays the mode's.
// Save and layout conventions (line ending, final newline, trimming, BOM,
// line length): its .editorconfig, with rustfmt.toml's max_width for Rust.
//

#ifndef NED_EDITOR_FILESETTINGS_H
#define NED_EDITOR_FILESETTINGS_H

#include <filesystem>
#include <string_view>

#include "Editor/IndentStyle.h"

namespace ned::text {
class Buffer;
class BufferList;
}

namespace ned::editor {

// `text` is the file's content, or its head and tail (ReadFileEnds).
[[nodiscard]] IndentOverride FileIndentOverride(const std::filesystem::path& path, std::string_view text);

// Sets buffer's LocalIndent and Conventions from its file on disk -- read
// directly rather than from the buffer, so an async-loading placeholder and
// a huge file get the same answer. A path that doesn't exist yet still gets
// its .editorconfig.
void ApplyFileSettings(text::Buffer& buffer);

// ApplyFileSettings again for every buffer whose file changed under it since
// (Buffer::FileGeneration: a revert, an external merge, a rename) -- the
// periodic external-change sweep calls it.
void RefreshFileSettings(text::BufferList& bufferList);

} // namespace ned::editor

#endif // NED_EDITOR_FILESETTINGS_H
