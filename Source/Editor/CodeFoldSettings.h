//
// One process-wide on/off switch for generic code folding's gutter
// affordance (CodeFold.h) -- mirrors TabWidth.h's exact pattern. Default on;
// configured from Janet (ned/set-code-folding-enabled). When off, BufferView
// shows no fold gutter column at all, even for a Mode with a real fold
// query -- distinct from a Mode having no fold query in the first place
// (Mode::fold empty), which already suppresses the gutter unconditionally.
//

#ifndef NED_EDITOR_CODEFOLDSETTINGS_H
#define NED_EDITOR_CODEFOLDSETTINGS_H

namespace ned::editor {

void               SetCodeFoldingEnabled(bool enabled);
[[nodiscard]] bool CodeFoldingEnabled();

} // namespace ned::editor

#endif // NED_EDITOR_CODEFOLDSETTINGS_H
