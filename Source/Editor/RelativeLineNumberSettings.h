//
// One process-wide on/off switch for relative line numbers in the gutter --
// mirrors CodeFoldSettings.h's exact pattern. Default off (absolute
// numbering); configured from Janet (ned/set-relative-line-numbers). When on,
// BufferView shows the current line's real number and every other visible
// line's distance from it (Vim's "relativenumber" convention).
//

#ifndef NED_EDITOR_RELATIVELINENUMBERSETTINGS_H
#define NED_EDITOR_RELATIVELINENUMBERSETTINGS_H

namespace ned::editor {

void               SetRelativeLineNumbersEnabled(bool enabled);
[[nodiscard]] bool RelativeLineNumbersEnabled();

} // namespace ned::editor

#endif // NED_EDITOR_RELATIVELINENUMBERSETTINGS_H
