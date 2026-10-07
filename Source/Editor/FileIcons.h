//
// The Nerd Font glyphs the chrome shows before a file's or folder's name,
// and whether it shows them. The setting is mutex-guarded static state, as
// WhitespaceSettings.h.
//

#ifndef NED_EDITOR_FILEICONS_H
#define NED_EDITOR_FILEICONS_H

#include <filesystem>
#include <string>

namespace ned::text {
class Buffer;
} // namespace ned::text

namespace ned::editor {

// Default true.
void               SetFileTypeIconsEnabled(bool enabled);
[[nodiscard]] bool FileTypeIconsEnabled();

// The language's icon (IconForPath), else a glyph for a common non-text
// kind (image, archive, font, ...), else a plain document. Empty while file
// type icons are off.
[[nodiscard]] std::string FileIcon(const std::filesystem::path& path);
// As above, preferring the icon of the mode the buffer actually runs.
[[nodiscard]] std::string FileIcon(const text::Buffer& buffer);
// Empty while file type icons are off.
[[nodiscard]] std::string FolderIcon(bool open);

} // namespace ned::editor

#endif // NED_EDITOR_FILEICONS_H
