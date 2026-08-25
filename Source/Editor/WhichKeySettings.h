//
// One process-wide on/off switch for the which-key prefix-hint popup --
// mirrors CodeFoldSettings.h's exact pattern. Default on; configured from
// Janet (ned/set-which-key-enabled). When off, BufferView never builds or
// fires a WhichKeyHint at all, so main.cpp's popup never shows regardless of
// a pending prefix chord -- the status line's own "C-x-" text (independent
// of this setting) is still shown either way.
//

#ifndef NED_EDITOR_WHICHKEYSETTINGS_H
#define NED_EDITOR_WHICHKEYSETTINGS_H

namespace ned::editor {

void               SetWhichKeyEnabled(bool enabled);
[[nodiscard]] bool WhichKeyEnabled();

} // namespace ned::editor

#endif // NED_EDITOR_WHICHKEYSETTINGS_H
