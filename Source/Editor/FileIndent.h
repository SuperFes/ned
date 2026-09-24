//
// A buffer's own indent style, taken from the file it shows: what its
// content already does (IndentDetection), then its .editorconfig
// (EditorConfigEnabled), then a Vim/Emacs modeline, each overriding the one
// before field by field. Whatever none of them states stays the mode's.
//

#ifndef NED_EDITOR_FILEINDENT_H
#define NED_EDITOR_FILEINDENT_H

#include <filesystem>
#include <string_view>

#include "Editor/IndentStyle.h"

namespace ned::text {
class Buffer;
}

namespace ned::editor {

// `text` is the file's content, or its head and tail (ReadFileEnds).
[[nodiscard]] IndentOverride FileIndentOverride(const std::filesystem::path& path, std::string_view text);

// Sets buffer's LocalIndent from its file on disk -- read directly rather
// than from the buffer, so an async-loading placeholder and a huge file get
// the same answer. A path that doesn't exist yet still gets .editorconfig.
void ApplyFileIndent(text::Buffer& buffer);

} // namespace ned::editor

#endif // NED_EDITOR_FILEINDENT_H
