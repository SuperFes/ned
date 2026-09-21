//
// search-everywhere-preview follow-up (ROADMAP.md, "Search-everywhere
// preview pane"): the pure half of showing what a search-everywhere row
// points at -- turning a handful of raw source lines into the lines the
// popup's footer paints.
//
// Pure and I/O-free, the same split Editor/RenameReview.h takes:
// everything here works over plain strings, so it is unit-testable with no
// Buffer, no file and no Screen. BufferView owns the I/O half -- resolving
// a candidate to a file and a line, reading that window out of the live
// buffer or off disk, and handing the result to the popup.
//
// The formatting is what makes a four-line slice readable in a popup
// footer rather than merely present: a deeply nested excerpt arrives with
// most of its width spent on indentation that says nothing once the
// surrounding scope is off-screen anyway, so the indentation common to the
// whole window is removed and each line keeps only what distinguishes it.
//

#ifndef NED_EDITOR_SEARCHEVERYWHEREPREVIEW_H
#define NED_EDITOR_SEARCHEVERYWHEREPREVIEW_H

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace ned::editor {

// The window the I/O half reads around a target line, and the per-line
// width past which a line is cut. kSearchEverywherePreviewLines is
// ui::ListPopup::kPreviewMaxLines' own budget, restated here because this
// module must not depend on the widget; a static_assert at the one call
// site that knows both keeps them honest.
constexpr std::size_t kSearchEverywherePreviewLines       = 4;
constexpr std::size_t kSearchEverywherePreviewLinesBefore = 1;
constexpr std::size_t kSearchEverywherePreviewMaxColumns  = 400;

// Display-ready lines for `rawLines`, which the caller has already windowed
// to at most kSearchEverywherePreviewLines entries (newlines stripped; a
// trailing \r is removed here).
//
// targetIndex, when set, is the entry `rawLines` was centred on -- it gets
// a leading marker and the others get matching blank padding, so a Text or
// Symbol row says which of the lines it actually matched. Unset means the
// window has no distinguished line (a File row previewing a file's opening
// lines) and no marker column is spent at all.
//
// tabWidth expands a leading or interior tab, since a popup footer paints
// one cell per codepoint and a raw \t would otherwise collapse the
// alignment the excerpt is being shown for.
[[nodiscard]] std::vector<std::string> FormatSearchEverywherePreview(const std::vector<std::string>& rawLines,
                                                                     std::optional<std::size_t>      targetIndex,
                                                                     std::size_t                     tabWidth);

} // namespace ned::editor

#endif // NED_EDITOR_SEARCHEVERYWHEREPREVIEW_H
