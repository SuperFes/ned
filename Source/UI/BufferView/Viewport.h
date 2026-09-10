//
// Where the view is looking, and the line/row arithmetic that depends on it --
// see Docs/BufferViewDecomposition.md.
//
// A buffer is a sequence of lines; a terminal shows a window of *rows*. Those
// are not the same thing, and three separate features pull them apart: folding
// hides lines outright, line wrapping turns one line into several rows, and
// narrowing restricts which lines exist as far as the view is concerned. Almost
// every question the rest of the view asks -- what is at this screen position,
// how far can we scroll, is point off-screen -- is really a question about that
// mapping.
//
// This owns the scroll position and the caches that make the mapping cheap to
// evaluate per frame: which line ranges are currently hidden, how many rows each
// line occupies at the current width, and (because wrapping has to know where
// link text sits) the parsed links.
//
// Three things it deliberately does not own, and takes instead:
//
//   - The widget's size, which belongs to the layout, not to us.
//   - The gutter's width, since content width is what is left after it. That
//     depends on which gutter columns are populated, which is GutterModel's
//     business.
//   - The foldable blocks, likewise GutterModel's, needed to work out which
//     lines a collapsed block hides.
//
// All three are asked for fresh rather than stored, because all three change
// without anything here being told.
//

#ifndef NED_UI_BUFFERVIEW_VIEWPORT_H
#define NED_UI_BUFFERVIEW_VIEWPORT_H

#include <cstddef>
#include <functional>
#include <utility>
#include <vector>

#include "Editor/Org.h"
#include "Text/Buffer.h"
#include "Text/ITextStorage.h"
#include "UI/BufferView/CacheStamp.h"
#include "UI/BufferView/EditorContext.h"
#include "UI/BufferView/GutterModel.h"
#include "UI/BufferView/RenderTypes.h"
#include "UI/Widget.h"

namespace ned::ui::bufferview {

class Viewport {
  public:
    // What the viewport has to ask the view for, because all of it is decided
    // during rendering and changes without the viewport being told. This is
    // where the row arithmetic stops being purely about the buffer: a line's
    // height on screen includes any inline diagnostic or code lens rows drawn
    // around it, and the usable height excludes the sticky header rows.
    struct Host {
        std::function<Size()>                        size;
        std::function<std::size_t()>                 gutterWidth;
        std::function<int()>                         stickyRowCount;
        std::function<std::size_t(std::size_t line)> annotationRows;
        std::function<std::size_t(std::size_t line)> leadingAnnotationRows;
        // Inlay hints on one line. They render as extra cells *before* the
        // real characters they annotate, so the horizontal-scroll decision
        // and the click-to-offset mapping both have to count them -- leaving
        // them out put point's column short by the width of every hint to
        // its left. Unset is a safe no-op (an empty list), like every other
        // hook here.
        std::function<std::vector<RenderedInlayHint>(std::size_t lineStart, std::size_t lineEnd)> inlayHintsForLine;
        // Scrolling invalidates a hover popup anchored to a screen position.
        std::function<void()> dismissHover;
    };

    Viewport(EditorContext& context, const GutterModel& gutters, Host host) : context_(context), gutters_(gutters), host_(std::move(host)) {
    }

    Viewport(const Viewport&)            = delete;
    Viewport& operator=(const Viewport&) = delete;

    // First visible buffer line, 0-indexed. Not clamped on assignment: callers
    // that need it bounded go through the scroll helpers below.
    [[nodiscard]] std::size_t TopLine() const {
        return topLine_;
    }
    void SetTopLine(std::size_t line);

    // Horizontal scroll, in display columns. Only meaningful when wrapping is
    // off; with wrapping on there is nothing to scroll past.
    [[nodiscard]] std::size_t LeftColumn() const {
        return leftColumn_;
    }
    void SetLeftColumn(std::size_t column) {
        leftColumn_ = column;
    }

    // Whether wrapping applies to the current buffer: the mode's default,
    // overridden per file extension or name where one is configured.
    [[nodiscard]] bool EffectiveWrapLines() const;

    // The furthest down scrolling may go before the last row of content sits at
    // the bottom of the view.
    [[nodiscard]] std::size_t MaxTopLine() const;

    // The lines currently in scope: the whole buffer, or the narrowed region.
    [[nodiscard]] std::pair<std::size_t, std::size_t> NarrowedLineRange() const;

    // How much of a huge buffer's content the structural queries may look at,
    // centred on what is on screen. Always the whole thing for an ordinary
    // buffer. GutterModel is given this as its StructuralWindowFn.
    [[nodiscard]] std::pair<std::size_t, std::size_t> HugeStructuralWindow(const text::ITextStorage& content) const;

    // --- folded-line awareness ----------------------------------------------
    [[nodiscard]] bool IsLineHidden(std::size_t line) const;
    // The first visible line at or after `line`, never past `limit`.
    [[nodiscard]] std::size_t NextVisibleLine(std::size_t line, std::size_t limit) const;
    // `count` visible lines forward from `line`, stopping at `limit`.
    [[nodiscard]] std::size_t AdvanceVisibleLines(std::size_t line, std::size_t count, std::size_t limit) const;
    [[nodiscard]] std::size_t VisibleLineCountBetween(std::size_t startLine, std::size_t endLineExclusive) const;

    // --- wrapped-row arithmetic ---------------------------------------------
    // Rows line `line` occupies: always 1 when wrapping is off.
    [[nodiscard]] std::size_t RowsForLine(std::size_t line) const;
    [[nodiscard]] std::size_t VisibleRowCountBetween(std::size_t startLine, std::size_t endLineExclusive) const;
    // Cheaper than VisibleRowCountBetween when the answer only has to clear a
    // threshold: stops counting once it does.
    [[nodiscard]] bool VisibleRowCountAtLeast(std::size_t startLine, std::size_t endLineExclusive,
                                              std::size_t limit) const;

    // --- scrolling ----------------------------------------------------------
    void ScrollToShowPoint();
    void ScrollToShowOffset(std::size_t offset);
    void ScrollToShowPointHorizontally();

    // Reset the scroll position when the pane switches to a different buffer,
    // restoring that buffer's remembered place where there is one.
    void EnsureTopLineValidForActiveBuffer();

    // Apply the buffer a pane *starts* on its remembered viewport. The switch
    // seam above only ever fires on a later switch, so without this the first
    // buffer would always open scrolled to the top even with a place recorded.
    void RestoreInitialPlace();

    // The byte offset under a screen position local to the content area,
    // clamped into the buffer when it falls outside any line.
    [[nodiscard]] std::size_t ByteOffsetForPoint(Point at) const;

    // The buffer's parsed org links. Here rather than in GutterModel because
    // wrapping has to know where link text sits to break around it.
    [[nodiscard]] const std::vector<editor::org::Link>& Links() const;

    // The [firstLine, lastLineExclusive) ranges hidden by collapsed folds.
    [[nodiscard]] const std::vector<std::pair<std::size_t, std::size_t>>& HiddenLineRanges() const;

  private:
    void EnsureHiddenLineRanges() const;
    void EnsureRowCounts() const;
    void EnsureLinks() const;

    EditorContext&     context_;
    const GutterModel& gutters_;
    Host               host_;

    std::size_t topLine_    = 0;
    std::size_t leftColumn_ = 0;
    // Which buffer topLine_ was last validated against, so a genuine switch is
    // distinguishable from the first paint.
    text::Buffer* topLineValidatedBuffer_ = nullptr;

    mutable CacheStamp                                       hiddenLineRangesStamp_;
    mutable std::vector<std::pair<std::size_t, std::size_t>> hiddenLineRanges_;

    // Sentinel for a line whose row count has not been worked out yet: the
    // cache is sized up front and filled in lazily per line as rows are asked
    // for, rather than wrapping the whole buffer on every resize.
    static constexpr std::size_t kRowCountUnknown = static_cast<std::size_t>(-1);

    mutable CacheStamp               rowCountStamp_;
    mutable int                      rowCountContentWidth_ = 0;
    mutable std::vector<std::size_t> rowCountPerLine_;

    mutable CacheStamp                     linkStamp_;
    mutable std::vector<editor::org::Link> links_;
};

} // namespace ned::ui::bufferview

#endif // NED_UI_BUFFERVIEW_VIEWPORT_H
