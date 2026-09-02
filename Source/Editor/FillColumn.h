//
// The target line width (in columns) fill-paragraph (M-q, Fill.h) wraps
// prose/comments to. One process-wide setting, not per-mode/per-buffer --
// the same "one process-wide choice" scope cut TabWidth.h/ProjectSearch.h's
// thread-count already make. Defaults to 70, matching Emacs' own
// fill-column default. Configured from Janet (ned/set-fill-column).
//

#ifndef NED_EDITOR_FILLCOLUMN_H
#define NED_EDITOR_FILLCOLUMN_H

namespace ned::editor {

void              SetFillColumn(int columns);
[[nodiscard]] int FillColumn();

} // namespace ned::editor

#endif // NED_EDITOR_FILLCOLUMN_H
