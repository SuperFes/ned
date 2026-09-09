//
// Where each gutter column sits for one frame -- see
// Docs/BufferViewDecomposition.md.
//
// The gutter is a row of optional columns, left to right:
//
//     [dap][diff][status][diagnostic][gap][digits][gap][test][coverage][symbol][fold][blame]
//
// Each is present only when it has something to show, so every column's start
// offset depends on which of the ones before it are active. That arithmetic was
// written out twice -- once in GutterWidth to total it up, and again at the top
// of Paint to place the columns -- with a comment claiming the second was
// "recomputing the same condition, not a second source of truth". It was exactly
// a second source of truth: the two had to agree, and nothing made them.
//
// One computation now produces both, so the width the layout reserves and the
// offsets it draws at cannot disagree.
//

#ifndef NED_UI_BUFFERVIEW_GUTTERLAYOUT_H
#define NED_UI_BUFFERVIEW_GUTTERLAYOUT_H

#include <cstddef>

namespace ned::ui::bufferview {

struct GutterLayout {
    // Width of each column, zero when it is not being drawn this frame.
    std::size_t dapWidth      = 0;
    std::size_t diffWidth     = 0;
    std::size_t testWidth     = 0;
    std::size_t coverageWidth = 0;
    std::size_t symbolWidth   = 0;
    std::size_t foldWidth     = 0;
    std::size_t blameWidth    = 0;
    // The line-number column is a gap, the digits, and another gap; all three
    // collapse together when line numbers are off.
    std::size_t lineNumberGap = 0;
    std::size_t digits        = 0;

    // Column offset from the left edge of the pane.
    std::size_t diffStart       = 0;
    std::size_t statusStart     = 0;
    std::size_t diagnosticStart = 0;
    std::size_t digitsStart     = 0;
    std::size_t testStart       = 0;
    std::size_t coverageStart   = 0;
    std::size_t symbolStart     = 0;
    std::size_t foldStart       = 0;
    std::size_t blameStart      = 0;

    // Total columns the gutter occupies; the content area is what is left.
    std::size_t totalWidth = 0;
};

} // namespace ned::ui::bufferview

#endif // NED_UI_BUFFERVIEW_GUTTERLAYOUT_H
