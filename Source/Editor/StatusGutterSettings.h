//
// The three process-wide switches for the 1-column status gutter --
// mirrors CodeFoldSettings.h's exact pattern.
//
// The column shows one of two things depending on the buffer: the
// unsaved-change swatch on a writable buffer, the unseen-content marker on
// a read-only buffer that content has been appended to since it was last
// looked at (a log, task output, test results). Neither is meaningful on
// the other's buffers, so they never compete for the cell.
//

#ifndef NED_EDITOR_STATUSGUTTERSETTINGS_H
#define NED_EDITOR_STATUSGUTTERSETTINGS_H

namespace ned::editor {

// Default true: a swatch per line edited since load/save, the conventional
// modified-line gutter marker.
void               SetUnsavedChangeSwatchEnabled(bool enabled);
[[nodiscard]] bool UnsavedChangeSwatchEnabled();

enum class UnseenContentMarkerStyle {
    // Every unseen line carries the swatch. Appends are the only thing that
    // can make content unseen, so the marked region is always a contiguous
    // tail and reads as a band with one clean top edge.
    Band,
    // Only the first unseen line carries it -- a "you left off here" rule
    // rather than a band, for a busy log where the band would be most of
    // the screen.
    Boundary,
};

// Default true. Costs nothing on a buffer that never participates: the
// marker needs a read-only buffer that Buffer::AppendWhileReadOnly has
// grown *and* that has been looked at and left at least once, which is
// what keeps a one-shot generated report from marking itself.
void               SetUnseenContentMarkerEnabled(bool enabled);
[[nodiscard]] bool UnseenContentMarkerEnabled();

void                                   SetUnseenContentMarkerStyle(UnseenContentMarkerStyle style);
[[nodiscard]] UnseenContentMarkerStyle GetUnseenContentMarkerStyle();

} // namespace ned::editor

#endif // NED_EDITOR_STATUSGUTTERSETTINGS_H
