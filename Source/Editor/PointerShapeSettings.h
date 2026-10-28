//
// The process-wide switch for UI/PointerShape.h's resize-pointer shapes,
// kept here so the Janet binding needn't reach into UI.
//

#ifndef NED_EDITOR_POINTERSHAPESETTINGS_H
#define NED_EDITOR_POINTERSHAPESETTINGS_H

namespace ned::editor {

// Default true. Off, the pointer returns to the terminal's default on the
// next mouse event.
void               SetPointerShapesEnabled(bool enabled);
[[nodiscard]] bool PointerShapesEnabled();

} // namespace ned::editor

#endif // NED_EDITOR_POINTERSHAPESETTINGS_H
