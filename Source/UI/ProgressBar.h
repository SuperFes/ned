//
// A determinate progress bar: a fraction that is actually known, unlike the
// mode line's spinner, which is the right answer for work whose end isn't.
//
// Two entry points on purpose. ProgressGlyphs is the bar itself, as a run of
// cells, for a caller that already owns its own row and paints flat columns
// into it -- the mode line, where a buffer's load and save progress live.
// ProgressBar is the same run inside a real Widget, painted through a themed
// Surface, for anything given a box of its own. One rendering rule, two
// drivers, so an inline bar and a standalone one can't drift apart.
//

#ifndef NED_UI_PROGRESSBAR_H
#define NED_UI_PROGRESSBAR_H

#include <string>
#include <string_view>
#include <vector>

#include "Theme.h"
#include "Widget.h"

namespace ned::ui {

// The bar `width` cells wide, filled to `fraction` (clamped to 0..1; a NaN
// reads as 0). The leading cell is an eighth-block glyph rather than a whole
// one, so the edge advances in sub-cell steps -- at ten cells that is eighty
// distinguishable positions instead of ten, which is the difference between
// a slow bar creeping and a slow bar looking frozen. Cells past the edge get
// `track`. Returns exactly `width` entries, each one cell wide (empty for a
// width <= 0).
[[nodiscard]] std::vector<std::string> ProgressGlyphs(double fraction, int width, std::string_view track = " ");

// How many of those cells belong to the filled run, counting a partially
// filled leading cell -- what a caller coloring the run needs, since the
// partial glyph is drawn in the fill color against the track behind it.
[[nodiscard]] int ProgressFilledCells(double fraction, int width);

// "45%" for 0.45, clamped the same way. Rounds toward zero so nothing reads
// 100% before it is actually done, which is the one number a progress
// indicator must never lie about.
[[nodiscard]] std::string ProgressPercentText(double fraction);

class ProgressBar : public Widget {
  public:
    // theme must outlive this widget, the usual convention here.
    explicit ProgressBar(const Theme& theme);

    // Synced fresh by whoever owns the real work, the same
    // recompute-don't-cache convention ScrollBar's own public fields use.
    double      fraction = 0.0;
    std::string label; // optional; drawn to the left of the bar

    // Drops pieces rather than overflowing as the box narrows: the percent
    // goes first, then the label, leaving a bar that still reads at the
    // handful of columns a cramped layout can spare.
    void Paint(Canvas c) override;

  private:
    const Theme& theme_;
};

} // namespace ned::ui

#endif // NED_UI_PROGRESSBAR_H
