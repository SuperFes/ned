#include "UI/BufferView/Viewport.h"

#include <algorithm>
#include <optional>
#include <string>

#include "Editor/CodeFold.h"
#include "Editor/Multibuffer.h"
#include "Editor/Org.h"
#include "Editor/Session.h"
#include "Editor/TabWidth.h"
#include "Editor/WrapOverrides.h"
#include "Text/Utf8.h"
#include "UI/ActiveBuffer.h"
#include "UI/BufferView/Internal.h"

namespace ned::ui::bufferview {

using namespace detail;

std::pair<std::size_t, std::size_t> Viewport::HugeStructuralWindow(const text::ITextStorage& content) const {
    const std::size_t byteLength = content.ByteLength();
    if (!content.IsHuge()) {
        return {0, byteLength};
    }

    const std::size_t margin         = editor::HugeStructuralWindowBytes();
    const std::size_t lineCount      = content.LineCount();
    const std::size_t lastLine       = lineCount > 0 ? lineCount - 1 : 0;
    const std::size_t topLine        = std::min(topLine_, lastLine);
    const std::size_t viewportHeight = host_.size().height > 0 ? static_cast<std::size_t>(host_.size().height) : 1;
    const std::size_t bottomLine     = std::min(topLine + viewportHeight, lastLine);

    const std::size_t viewportStartByte = content.LineToByteOffset(topLine);
    const std::size_t viewportEndByte   = std::min(content.LineToByteOffset(bottomLine) + 1, byteLength);

    // Snapped to the start of whatever line it lands in, not a raw byte
    // subtraction -- confirmed empirically (not assumed) that feeding
    // tree-sitter a substring beginning mid-line/mid-token can desync its
    // parse badly enough to misidentify a spurious definition right at the
    // cut, or silently miss a real one shortly after it, well beyond the
    // "truncated at the tail" class of imprecision the trailing edge alone
    // has to worry about. A clean line start costs at most one extra line
    // of margin and gives the parser real, syntactically valid context from
    // its very first byte.
    const std::size_t rawWindowStart = viewportStartByte > margin ? viewportStartByte - margin : 0;
    const std::size_t windowStart    = content.LineToByteOffset(content.ByteOffsetToLine(rawWindowStart));
    const std::size_t windowEnd      = byteLength - viewportEndByte > margin ? viewportEndByte + margin : byteLength;
    return {windowStart, windowEnd};
}

void Viewport::EnsureHiddenLineRanges() const {
    text::Buffer&    buffer = context_.activeBuffer.Get();
    const CacheStamp stamp =
        CacheStamp::For(&buffer, {buffer.ContentGeneration(), buffer.FoldGeneration()});

    if (buffer.FoldMarkers().empty()) {
        // Fast path: every buffer that's never had org-cycle/code-fold-toggle
        // run at all (i.e. every non-Org, non-folded buffer) takes this
        // branch -- no call into org::FoldedLineRanges/codefold::FoldedLineRanges,
        // no outline re-parse or fold-query call, at all.
        hiddenLineRanges_.clear();
        hiddenLineRangesStamp_ = stamp;
        return;
    }

    if (hiddenLineRangesStamp_.Matches(stamp)) {
        return;
    }

    // Auto-collapse-on-build follow-up: a multibuffer (*vcs diff*/*vcs
    // commit*/*references*/*diagnostics*/...) is checked first -- its
    // context_.mode is always a plain FundamentalMode (no fold query, path-less
    // buffer), so it would otherwise fall through to the org branch below
    // and never show any of BuildMultibuffer's own auto-collapsed excerpts.
    // FoldableExcerptBlocks derives codefold::FoldedLineRanges' own "blocks"
    // shape from the excerpt spans themselves -- no tree-sitter/mode
    // involved, same function either way.
    if (editor::multibuffer::MultibufferIndex* index = editor::multibuffer::MultibufferIndexFor(buffer)) {
        hiddenLineRanges_ =
            editor::codefold::FoldedLineRanges(buffer, buffer.Content(), editor::multibuffer::FoldableExcerptBlocks(*index));
    }
    // generic-code-folding follow-up: only a mode with a real fold query
    // goes through the new generic tree-sitter-block path -- every other
    // mode (org-mode included, which has none: it drives FoldMarkers_
    // entirely through org::CycleFoldAtPoint, mode-independently) keeps the
    // original org::FoldedLineRanges path exactly as before. This isn't
    // really "Org-specific" so much as "the only interpretation that
    // existed before this feature" -- preserving it for every mode::fold-
    // less buffer is what keeps pre-existing direct-FoldMarkers_ usage
    // (e.g. a plain FundamentalMode buffer with a marker set by hand)
    // working exactly as it always has.
    else if (context_.mode.fold) {
        hiddenLineRanges_ = editor::codefold::FoldedLineRanges(buffer, buffer.Content(), gutters_.FoldableBlocks());
    }
    else {
        hiddenLineRanges_ = editor::org::FoldedLineRanges(buffer);
    }
    hiddenLineRangesStamp_ = stamp;
}

void Viewport::EnsureLinks() const {
    if (context_.mode.name != "org-mode") {
        // Fast path: every non-Org buffer never even reaches org::ParseLinks
        // -- see this cache's own doc comment in BufferView.h.
        links_.clear();
        linkStamp_.Invalidate();
        return;
    }

    text::Buffer&    buffer = context_.activeBuffer.Get();
    const CacheStamp stamp  = CacheStamp::For(&buffer, {buffer.ContentGeneration()});
    if (linkStamp_.Matches(stamp)) {
        return;
    }

    links_     = editor::org::ParseLinks(buffer.Text());
    linkStamp_ = stamp;
}

bool Viewport::IsLineHidden(std::size_t line) const {
    EnsureHiddenLineRanges();
    for (const auto& [start, end] : hiddenLineRanges_) {
        if (line >= start && line < end)
            return true;
    }
    return false;
}

std::size_t Viewport::NextVisibleLine(std::size_t line, std::size_t limit) const {
    while (line < limit && IsLineHidden(line))
        ++line;
    return line;
}

std::size_t Viewport::AdvanceVisibleLines(std::size_t line, std::size_t count, std::size_t limit) const {
    while (count > 0 && line < limit) {
        line = NextVisibleLine(line + 1, limit);
        --count;
    }
    return line;
}

std::size_t Viewport::VisibleLineCountBetween(std::size_t startLine, std::size_t endLineExclusive) const {
    std::size_t count = 0;
    for (std::size_t line = startLine; line < endLineExclusive; ++line) {
        if (!IsLineHidden(line))
            ++count;
    }
    return count;
}

void Viewport::EnsureRowCounts() const {
    text::Buffer&     buffer       = context_.activeBuffer.Get();
    const bool        wrapEnabled  = EffectiveWrapLines();
    const std::size_t gutterWidth  = host_.gutterWidth();
    const int         contentWidth = std::max(1, host_.size().width - static_cast<int>(gutterWidth));

    const CacheStamp stamp = CacheStamp::For(
        &buffer, {buffer.ContentGeneration(), static_cast<std::size_t>(contentWidth), static_cast<std::size_t>(wrapEnabled)});

    if (!wrapEnabled) {
        // Fast path: every buffer with wrap off (the common case) never
        // needs a real per-line row count at all -- RowsForLine's own "1
        // when !wrapEnabled" branch below never even looks at
        // rowCountPerLine_ in that case, so this just keeps the cache keys
        // themselves current without ever calling ComputeWrapSegments.
        rowCountPerLine_.clear();
        rowCountContentWidth_ = contentWidth;
        rowCountStamp_        = stamp;
        return;
    }

    if (rowCountStamp_.Matches(stamp)) {
        return; // still valid -- whatever's already memoized in rowCountPerLine_ (per RowsForLine) stays
    }

    // line-wrap follow-up: only resets the cache's sizing/keys here (a
    // cheap sentinel fill, not real work) -- RowsForLine below is what
    // actually computes and memoizes one line's row count, lazily, the
    // first time that specific line is asked about. An earlier version
    // eagerly computed every line's real word-break scan right here, which
    // a [Performance] test caught as a genuine regression: MaxTopLine()/
    // ScrollToShowPoint() run every Paint() call, so an eager whole-buffer
    // scan here made every single Paint() call on a huge wrap-enabled
    // document pay for the full document's word-break cost up front.
    rowCountPerLine_.assign(buffer.Content().LineCount(), kRowCountUnknown);
    rowCountContentWidth_ = contentWidth;
    rowCountStamp_        = stamp;
}

std::size_t Viewport::RowsForLine(std::size_t line) const {
    if (IsLineHidden(line)) {
        return 0; // hidden hides the annotation row too -- a fold swallows the whole line
    }
    // inline-diagnostics follow-up: an annotated line reports one extra row
    // here, at the single source every row-math consumer already shares
    // (CursorPosition, ScrollToShowPoint, MaxTopLine, ByteOffsetForPoint,
    // VisibleRowCountBetween/AtLeast) -- the same seam wrap continuation
    // rows ride, so none of them can disagree about where an annotation
    // shifted the rows below it.
    const std::size_t annotationRows = host_.annotationRows(line);
    // codeLens follow-up: LeadingAnnotationRowsForLine's own 0-or-1 count,
    // added at the same three return points annotationRows already is --
    // see that method's own doc comment for why this is a leading
    // (above-the-line) row rather than another trailing one.
    const std::size_t leadingRows = host_.leadingAnnotationRows(line);
    if (!EffectiveWrapLines()) {
        return 1 + leadingRows + annotationRows;
    }
    EnsureRowCounts();
    if (line >= rowCountPerLine_.size()) {
        return 1 + leadingRows + annotationRows;
    }
    if (rowCountPerLine_[line] == kRowCountUnknown) {
        // line-wrap follow-up: the real, lazy, per-line word-break scan --
        // computed and memoized only for a line actually asked about, never
        // eagerly for the whole buffer (see EnsureRowCounts's own doc
        // comment for why that distinction is load-bearing, not cosmetic).
        text::Buffer&             buffer    = context_.activeBuffer.Get();
        const text::ITextStorage& content   = buffer.Content();
        const std::size_t         lineStart = content.LineToByteOffset(line);
        const std::size_t         lineEnd =
            (line + 1 < content.LineCount()) ? content.LineToByteOffset(line + 1) - 1 : content.ByteLength();
        EnsureLinks();
        const std::vector<RenderedLink> lineLinks = LinksForLine(links_, lineStart, lineEnd, buffer.Point());
        rowCountPerLine_[line] =
            ComputeWrappedLineSegments(content, lineStart, lineEnd, rowCountContentWidth_, lineLinks).size();
    }
    return rowCountPerLine_[line] + leadingRows + annotationRows; // memoized value is content rows only -- annotation state changes independently of the wrap cache's keys
}

std::size_t Viewport::VisibleRowCountBetween(std::size_t startLine, std::size_t endLineExclusive) const {
    std::size_t count = 0;
    for (std::size_t line = startLine; line < endLineExclusive; ++line) {
        count += RowsForLine(line);
    }
    return count;
}

bool Viewport::VisibleRowCountAtLeast(std::size_t startLine, std::size_t endLineExclusive, std::size_t limit) const {
    std::size_t count = 0;
    for (std::size_t line = startLine; line < endLineExclusive; ++line) {
        count += RowsForLine(line);
        if (count >= limit) {
            return true;
        }
    }
    return false;
}

void Viewport::EnsureTopLineValidForActiveBuffer() {
    text::Buffer& buffer = context_.activeBuffer.Get();
    if (topLineValidatedBuffer_ == &buffer) {
        // A place restored at construction was clamped by point's own line
        // rather than by MaxTopLine(), which was unknowable then (0x0 widget).
        // Redo it properly now that there is a size: a file that shrank
        // between runs clamps point to its new last line, and a topLine_ equal
        // to that line puts it on the top row with nothing but past-the-end
        // rows beneath -- a blank pane. Real report: ROADMAP.md, pruned from
        // 2301 lines to 1617, reopened empty.
        if (topLineNeedsSizeClamp_ && host_.size().height > 0) {
            topLineNeedsSizeClamp_ = false;
            topLine_               = std::min(topLine_, MaxTopLine());
        }
        return;
    }
    topLineNeedsSizeClamp_ = false;
    topLineValidatedBuffer_ = &buffer;
    host_.dismissHover(); // hover-tooltips follow-up: a tooltip from the previous buffer means nothing here
    // session-persistence slice 1: a stored viewport for this buffer wins
    // over whatever topLine_ the previous buffer left behind -- this seam
    // fires exactly once per buffer switch (and on a pane's very first
    // Paint, covering the startup buffer), which is what makes restored
    // scroll positions land here and nowhere else. Clamped by MaxTopLine()
    // against content that shrank since the place was recorded (an outside
    // edit between runs); ScrollToShowPoint() below then still guarantees
    // point is visible, so a topLine/point pair that somehow disagrees
    // resolves in point's favor, never a blank or point-less view.
    if (const auto place = editor::StoredFilePlaceFor(buffer); place && place->topLine) {
        topLine_ = std::min(*place->topLine, MaxTopLine());
    }
    else {
        // Real reported bug (LSP log buffer): topLine_ left behind by a
        // much longer previous buffer (say line 50) could exceed this
        // buffer's own MaxTopLine() (0, if its whole handful of lines
        // fits in one viewport). ScrollToShowPoint()'s "point is above
        // topLine_" branch below then set topLine_ = pointLine exactly --
        // pinning point to the viewport's literal top row and leaving
        // every row beneath it blank, instead of clamping down to 0 and
        // showing the whole short buffer with point at its natural
        // (bottom) position. Clamping here first preserves the "leave
        // topLine_ alone when it already suits the new buffer" case just
        // below (topLine_ <= MaxTopLine() already, so this is a no-op)
        // while fixing the case where it doesn't.
        topLine_ = std::min(topLine_, MaxTopLine());
    }
    // ScrollToShowPoint() alone (no need to reset topLine_ to 0 first) is
    // already safe against topLine_ being an arbitrary leftover value from
    // whichever buffer was active before: its own "point is above topLine_"
    // branch fires unconditionally whenever pointLine < topLine_, which is
    // exactly what happens when topLine_ was left pointing well past the
    // newly active (and possibly much shorter) buffer's own last line --
    // this buffer's own pointLine is always a real, in-range line, so it's
    // always < an out-of-range topLine_. The one thing this deliberately
    // preserves rather than discarding: if topLine_ happens to already show
    // the new buffer's own point (e.g. switching between two similarly
    // long buffers), it's left exactly where it was instead of always
    // jumping back to the top.
    ScrollToShowPoint();
}

void Viewport::ScrollToShowPoint() {
    ScrollToShowOffset(context_.activeBuffer.Get().Point());
    ScrollToShowPointHorizontally();
}

void Viewport::ScrollToShowOffset(std::size_t offset) {
    const text::ITextStorage& content   = context_.activeBuffer.Get().Content();
    const std::size_t         pointLine = content.ByteOffsetToLine(offset);

    if (pointLine < topLine_) {
        // Clamped by MaxTopLine() rather than set to pointLine outright.
        // Real reported bug: a file that shrank between runs (ROADMAP.md,
        // pruned 2301 -> 1617 lines) clamps its restored point to the new
        // last line, and pinning *that* line to the top row leaves every row
        // beneath it past the end of the file -- a wholly blank pane. The
        // same failure EnsureTopLineValidForActiveBuffer's else-branch
        // already guards for a carried-over topLine_; a stored place fell
        // through to here instead. MaxTopLine() is by definition the largest
        // topLine_ that still fills the viewport, and point stays visible
        // because it sits at or below the last line, which MaxTopLine() puts
        // on the bottom row.
        topLine_ = std::min(pointLine, MaxTopLine());
    }
    else if (host_.size().height > 0) {
        // main-editor-sticky-scroll follow-up: same host_.stickyRowCount()
        // deduction MaxTopLine() applies now -- see its own doc comment.
        const auto        visibleLines  = static_cast<std::size_t>(std::max(0, host_.size().height - host_.stickyRowCount()));
        const std::size_t pointLineRows = RowsForLine(pointLine);
        // Org-mode fold/unfold follow-up: was `pointLine >= topLine_ +
        // visibleLines` / `topLine_ = pointLine - visibleLines + 1`, raw
        // buffer-line arithmetic that assumed every line between topLine_
        // and pointLine renders as its own row. line-wrap follow-up:
        // VisibleRowCountAtLeast is the fold-AND-wrap-aware "would
        // pointLine's own last row still fit" check (was
        // VisibleLineCountBetween, then VisibleRowCountBetween -- an
        // early-exit bounded check now, not an exact sum, so this never
        // walks more of a huge document than the viewport itself needs;
        // see VisibleRowCountAtLeast's own doc comment).
        // pointLineRows > visibleLines is checked first, short-circuiting
        // before the subtraction below could ever underflow (pointLine
        // itself taller than the whole viewport -- always needs a rescroll
        // regardless of what's above it).
        if (pointLineRows > visibleLines || VisibleRowCountAtLeast(topLine_, pointLine, visibleLines + 1 - pointLineRows)) {
            // line-wrap follow-up: was a partial-credit backward walk
            // (`remaining -= min(remaining, RowsForLine(newTop))`) that
            // could stop mid-line, silently discarding whatever didn't
            // fit -- topLine_ can only ever start at a line's own first
            // row (a documented scope cut, not mid-segment), so
            // "partially fitting" a line here doesn't correspond to
            // anything Paint() can actually render: it draws that line's
            // FULL row count regardless. Now walks backward including only
            // WHOLE lines that still fit alongside pointLine's own (always
            // fully included) rows, stopping before, not mid-way through,
            // one that wouldn't.
            std::size_t newTop      = pointLine;
            std::size_t accumulated = pointLineRows;
            while (newTop > 0) {
                const std::size_t candidate = newTop - 1;
                if (IsLineHidden(candidate)) {
                    newTop = candidate;
                    continue;
                }
                const std::size_t rows = RowsForLine(candidate);
                if (accumulated + rows > visibleLines) {
                    break;
                }
                accumulated += rows;
                newTop = candidate;
            }
            topLine_ = newTop;
        }
    }
    // multi-cursor-round-2 follow-up: no ScrollToShowPointHorizontally()
    // call here -- it always reads context_.activeBuffer.Get().Point() directly, so
    // calling it for an arbitrary secondary-cursor offset would scroll
    // horizontally to the *primary's* column instead, wrong for exactly the
    // case this parameterized form exists for. ScrollToShowPoint() (below)
    // still gets both, since it always wants "make sure point itself is
    // fully visible."
}

void Viewport::ScrollToShowPointHorizontally() {
    if (EffectiveWrapLines()) {
        return; // a wrapped line never extends past the viewport width -- nothing to scroll
    }

    const text::Buffer&       buffer      = context_.activeBuffer.Get();
    const text::ITextStorage& content     = buffer.Content();
    const std::size_t         point       = buffer.Point();
    const std::size_t         pointLine   = content.ByteOffsetToLine(point);
    const std::size_t         lineStart   = content.LineToByteOffset(pointLine);
    const std::size_t         gutterWidth = host_.gutterWidth();
    const Size                sizeNow     = host_.size();
    if (sizeNow.width <= 0) {
        return; // nothing meaningful to clamp against yet (e.g. before the first real layout)
    }
    const int contentWidth = std::max(1, sizeNow.width - static_cast<int>(gutterWidth));

    EnsureLinks();
    const std::size_t lineEnd =
        (pointLine + 1 < content.LineCount()) ? content.LineToByteOffset(pointLine + 1) - 1 : content.ByteLength();
    const std::vector<RenderedLink>      lineLinks = LinksForLine(links_, lineStart, lineEnd, point);
    const std::vector<RenderedInlayHint> lineHints =
        host_.inlayHintsForLine ? host_.inlayHintsForLine(lineStart, lineEnd) : std::vector<RenderedInlayHint>{};

    // Point's true column from the start of the line, unbounded (well,
    // bounded only by the line's own length, not the viewport) -- needed to
    // decide whether leftColumn_ has to move at all, so this can't reuse
    // VisualColumn's own maxColumns-bounded form directly; a pathologically
    // long line still only walks as far as point itself, same cost class as
    // every other per-line scan in this file.
    const std::optional<int> visualCol =
        VisualColumn(content, lineStart, point, std::numeric_limits<int>::max(), lineLinks, lineHints);
    if (!visualCol) {
        return; // shouldn't happen with an unbounded maxColumns, but a safe no-op
    }

    if (*visualCol < static_cast<int>(leftColumn_)) {
        leftColumn_ = static_cast<std::size_t>(*visualCol);
    }
    else if (*visualCol >= static_cast<int>(leftColumn_) + contentWidth) {
        leftColumn_ = static_cast<std::size_t>(*visualCol - contentWidth + 1);
    }
}

void Viewport::SetTopLine(std::size_t line) {
    const auto [rangeStart, rangeEnd] = NarrowedLineRange();
    // Org-mode fold/unfold follow-up: topLine_ must always land on a
    // visible line (every other fold-aware query here assumes that), so a
    // target sitting inside a hidden range rounds forward to the next
    // visible one before the usual clamp.
    line     = NextVisibleLine(std::max(line, rangeStart), rangeEnd);
    topLine_ = std::clamp(line, rangeStart, MaxTopLine());
}

bool Viewport::EffectiveWrapLines() const {
    return editor::EffectiveWrapLines(context_.activeBuffer.Get().Path(), context_.activeBuffer.Get().Name(), context_.mode);
}

std::pair<std::size_t, std::size_t> Viewport::NarrowedLineRange() const {
    const text::Buffer& buffer = context_.activeBuffer.Get();
    if (!buffer.IsNarrowed()) {
        return {0, buffer.Content().LineCount()};
    }
    const auto [narrowedStart, narrowedEnd] = buffer.NarrowedRange();
    const std::size_t startLine             = buffer.Content().ByteOffsetToLine(narrowedStart);
    // narrowedEnd is a byte offset at a line's own start (see
    // Buffer::NarrowToRegion's whole-line-snapping doc comment) -- except
    // when it's the buffer's own real end, which may fall mid-line. Either
    // way, the line *containing* the byte just before it is the narrowed
    // range's own last line; +1 makes this exclusive, matching
    // Content().LineCount()'s own convention.
    const std::size_t endLine = buffer.Content().ByteOffsetToLine(narrowedEnd > 0 ? narrowedEnd - 1 : 0) + 1;
    return {startLine, endLine};
}

std::size_t Viewport::MaxTopLine() const {
    const auto [rangeStart, rangeEnd] = NarrowedLineRange();
    // main-editor-sticky-scroll follow-up: the pinned rows from the last
    // Paint() eat into the pane's own real, drawable body height the exact
    // same way CursorPosition()/ByteOffsetForPoint already account for --
    // omitting this here undercounts how many rows the sticky band is
    // currently costing, so this method thought more content fit below
    // topLine_ than Paint() could actually draw, permanently stranding a
    // wrapped document's true last lines just past the bottom of the
    // viewport. One-frame-stale like every other host_.stickyRowCount() read
    // (Paint() recomputes it fresh every frame, so this self-corrects).
    const auto visibleLines =
        host_.size().height > 0 ? static_cast<std::size_t>(std::max(0, host_.size().height - host_.stickyRowCount())) : 0;
    // Org-mode fold/unfold follow-up: was `rangeStart + (totalLines >
    // visibleLines ? totalLines - visibleLines : 0)`, plain buffer-line
    // subtraction. line-wrap follow-up: VisibleRowCountAtLeast is the
    // fold-AND-wrap-aware "does everything already fit in one viewport"
    // check (was VisibleLineCountBetween, then an exact-sum
    // VisibleRowCountBetween -- an early-exit bounded check now, so this
    // never walks more of a huge document than the viewport itself could
    // need; see that method's own doc comment). Checking "at least
    // visibleLines + 1" is the negation of "the total is <= visibleLines."
    if (!VisibleRowCountAtLeast(rangeStart, rangeEnd, visibleLines + 1)) {
        return rangeStart;
    }
    // line-wrap follow-up: was a partial-credit backward walk
    // (`remaining -= min(remaining, RowsForLine(newTop))`) that could stop
    // mid-line, silently discarding whatever didn't fit -- topLine_ can
    // only ever start at a line's own first row (a documented scope cut,
    // not mid-segment), so "partially fitting" a line here doesn't
    // correspond to anything Paint() can actually render: it draws that
    // line's FULL row count regardless of how much of it "fit" in this
    // walk's own bookkeeping, which could silently push whatever comes
    // after it off the bottom of the viewport entirely. A real, reported
    // bug this fixes: scrolling to the end of a wrapped document could
    // leave its own last lines permanently unreachable, since a wrapped
    // line earlier in the walk could swallow the whole remaining budget
    // without actually being fully shown. Now walks backward including
    // only WHOLE lines that still fit within the budget, stopping before
    // (not mid-way through) one that wouldn't -- except the very first
    // real line considered, which is always included in full even if it
    // alone exceeds visibleLines (matching "always show at least the last
    // line" -- the same guarantee this walk already gave for free back
    // when every line was implicitly exactly 1 row).
    std::size_t newTop      = rangeEnd;
    std::size_t accumulated = 0;
    while (newTop > rangeStart) {
        const std::size_t candidate = newTop - 1;
        if (IsLineHidden(candidate)) {
            newTop = candidate;
            continue;
        }
        const std::size_t rows = RowsForLine(candidate);
        if (accumulated > 0 && accumulated + rows > visibleLines) {
            break;
        }
        accumulated += rows;
        newTop = candidate;
    }
    return newTop;
}

std::size_t Viewport::ByteOffsetForPoint(Point at) const {
    // Org-mode fold/unfold follow-up: was topLine_ + at.y, a flat 1:1
    // mapping -- a click on screen row N means the N-th *visible* buffer
    // line below topLine_, not literally topLine_ + N, whenever a fold is
    // hiding lines above the click. line-wrap follow-up: was
    // AdvanceVisibleLines (pure line-stepping, 1 row per visible line);
    // now a row-aware walk that consumes RowsForLine(line) rows per line
    // instead, additionally reporting which segment of the landed-on line
    // the target row corresponds to.
    const text::Buffer&       buffer     = context_.activeBuffer.Get();
    const text::ITextStorage& content    = buffer.Content();
    const std::size_t         totalLines = content.LineCount();

    // main-editor-sticky-scroll follow-up: at.y is in this pane's own local
    // coordinates, unaware that Paint() may have pushed real content down by
    // host_.stickyRowCount() rows this frame -- subtracted here so a click below
    // the pinned rows still resolves to the buffer line actually drawn
    // there. A click landing IN the pinned band itself (result would be
    // negative) clamps to row 0, same as the existing at.y<0 defensive
    // clamp below.
    std::size_t targetRow     = static_cast<std::size_t>(std::max(at.y - host_.stickyRowCount(), 0));
    std::size_t line          = topLine_;
    std::size_t segmentInLine = 0;
    while (line < totalLines) {
        const std::size_t rows = RowsForLine(line);
        if (rows == 0) {
            line = NextVisibleLine(line + 1, totalLines);
            continue;
        }
        if (targetRow < rows) {
            segmentInLine = targetRow;
            break;
        }
        targetRow -= rows;
        line = NextVisibleLine(line + 1, totalLines);
    }
    line = std::min(line, totalLines - 1); // mirrors Buffer::ByteOffsetForLineAndColumn's own clamp

    const std::size_t x           = static_cast<std::size_t>(std::max(at.x, 0));
    const std::size_t gutterWidth = host_.gutterWidth();
    // A click inside the gutter itself lands on that line's first column,
    // same as clicking right at the start of the line's text. line-wrap
    // follow-up: leftColumn_ added back on -- the click's on-screen column
    // has to be translated back to the line's own column space, the same
    // "screen column = real column - leftColumn_" relationship Paint()'s own
    // fast-forward phase established for drawing. Always 0 once
    // EffectiveWrapLines() is true, a no-op then.
    const std::size_t column = (x > gutterWidth) ? x - gutterWidth + leftColumn_ : leftColumn_;

    // Links follow-up: was a direct Buffer::ByteOffsetForLineAndColumn call
    // -- that method must stay entirely link-oblivious (Buffer has zero
    // Org-specific knowledge), so this reimplements its same tab-aware walk
    // locally, link-aware, via ByteOffsetForColumnInLine (see its own doc
    // comment above for why it's safe to call unconditionally here even in
    // a non-Org buffer). lineLinks is built against buffer.Point() -- the
    // buffer's point *before* this click resolves -- so it excludes exactly
    // the links Paint() left uncollapsed the last time this row was
    // actually drawn.
    const std::size_t lineStart = content.LineToByteOffset(line);
    const std::size_t lineEnd   = (line + 1 < totalLines) ? content.LineToByteOffset(line + 1) - 1 : content.ByteLength();
    EnsureLinks();
    const std::vector<RenderedLink> lineLinks = LinksForLine(links_, lineStart, lineEnd, buffer.Point());

    // line-wrap follow-up: resolve the click against the landed-on
    // segment's own [startByte, endByte) instead of the whole line's range
    // -- lineLinks is still the whole line's own set (matches Paint()'s own
    // "compute once per line, reuse per segment" shape), just the byte
    // range being searched narrows to this one row.
    std::size_t segStart = lineStart;
    std::size_t segEnd   = lineEnd;
    if (EffectiveWrapLines()) {
        const int                      fullWidth      = std::max(1, host_.size().width - static_cast<int>(gutterWidth));
        const std::vector<WrapSegment> segments       = ComputeWrappedLineSegments(content, lineStart, lineEnd, fullWidth, lineLinks);
        const std::size_t              clampedSegment = std::min(segmentInLine, segments.size() - 1);
        segStart                                      = segments[clampedSegment].startByte;
        segEnd                                        = segments[clampedSegment].endByte;
    }

    const std::vector<RenderedInlayHint> lineHints =
        host_.inlayHintsForLine ? host_.inlayHintsForLine(lineStart, lineEnd) : std::vector<RenderedInlayHint>{};
    return ByteOffsetForColumnInLine(content, segStart, segEnd, column, editor::TabWidth(), lineLinks, lineHints);
}

const std::vector<std::pair<std::size_t, std::size_t>>& Viewport::HiddenLineRanges() const {
    EnsureHiddenLineRanges();
    return hiddenLineRanges_;
}

const std::vector<editor::org::Link>& Viewport::Links() const {
    EnsureLinks();
    return links_;
}

void Viewport::RestoreInitialPlace() {
    // Seeding topLineValidatedBuffer_ here is what lets
    // EnsureTopLineValidForActiveBuffer tell a genuine switch to another buffer
    // from the very first paint, which would otherwise reset topLine_ and throw
    // away any scroll a wheel or scroll-bar event made before that paint -- a
    // real regression this exact seeding once introduced, caught by a test.
    topLineValidatedBuffer_ = &context_.activeBuffer.Get();

    // Clamped by the line point is on rather than MaxTopLine(), which is
    // meaningless here: the widget is still 0x0 at construction. A consistently
    // recorded place always has topLine <= pointLine, so the min only bites
    // when the file shrank outside ned and point was clamped -- pinning point's
    // own line to the top row is the sane view for that.
    if (const auto place = editor::StoredFilePlaceFor(context_.activeBuffer.Get()); place && place->topLine) {
        const text::Buffer& buffer    = context_.activeBuffer.Get();
        const std::size_t   pointLine = buffer.Content().ByteOffsetToLine(buffer.Point());
        topLine_                      = std::min(*place->topLine, pointLine);
        // See topLineNeedsSizeClamp_'s own comment: this min is a placeholder
        // for the MaxTopLine() clamp no size is available for yet.
        topLineNeedsSizeClamp_ = true;
    }
}

} // namespace ned::ui::bufferview
