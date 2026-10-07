//
// Whether the chrome shows a file's type as its language's Nerd Font glyph
// (Mode::icon). Mutex-guarded static state, as WhitespaceSettings.h.
//

#ifndef NED_EDITOR_ICONSETTINGS_H
#define NED_EDITOR_ICONSETTINGS_H

namespace ned::editor {

// Default true.
void               SetFileTypeIconsEnabled(bool enabled);
[[nodiscard]] bool FileTypeIconsEnabled();

} // namespace ned::editor

#endif // NED_EDITOR_ICONSETTINGS_H
