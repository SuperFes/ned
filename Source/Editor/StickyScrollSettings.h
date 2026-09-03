//
// Two process-wide settings for the main-editor sticky scroll (the pinned
// namespace/class/method breadcrumb rows BufferView draws at the top of a
// pane) -- mirrors CodeFoldSettings.h/TabWidth.h's exact pattern. Configured
// from Janet (ned/set-sticky-scroll-enabled, ned/set-sticky-scroll-max-rows).
//

#ifndef NED_EDITOR_STICKYSCROLLSETTINGS_H
#define NED_EDITOR_STICKYSCROLLSETTINGS_H

namespace ned::editor {

// Default true.
void               SetStickyScrollEnabled(bool enabled);
[[nodiscard]] bool StickyScrollEnabled();

// Caps how many pinned rows a pane will ever reserve for the breadcrumb,
// regardless of how deep the actual enclosing chain is -- a pathologically
// nested buffer shouldn't be able to eat the whole viewport. Default 3.
void              SetStickyScrollMaxRows(int rows);
[[nodiscard]] int StickyScrollMaxRows();

} // namespace ned::editor

#endif // NED_EDITOR_STICKYSCROLLSETTINGS_H
