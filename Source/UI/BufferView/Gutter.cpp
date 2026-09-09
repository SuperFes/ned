//
// Part of BufferView -- see UI/BufferView.h for the class itself and
// Docs/BufferViewDecomposition.md for why this file exists.
//
// The gutter/derived-data caches (every Ensure*Cache), the gutter column-layout
// queries, and the viewport geometry: scrolling, wrapping, and visible-line math.
//

#include "UI/BufferView/Internal.h"

namespace ned::ui {

// The file-local helpers these definitions call live in BufferView/Internal.h
// now that several parts share them -- see that header. This using-directive is
// what let the split leave every call site untouched.
using namespace detail;

void BufferView::EnsureEmbeddedDocumentCache() {
    text::Buffer& buffer = activeBuffer_.Get();

    if (!mode_.embeddedRegions) {
        embeddedDocumentCacheByBuffer_.erase(&buffer); // nothing to cache -- mode has no embedded regions at all
        return;
    }

    const auto it = embeddedDocumentCacheByBuffer_.find(&buffer);
    if (it != embeddedDocumentCacheByBuffer_.end() && it->second.contentGeneration == buffer.ContentGeneration() &&
        it->second.modeName == mode_.name) {
        return; // already current
    }

    EmbeddedDocumentCacheEntry entry;
    entry.documents         = editor::BuildEmbeddedDocuments(mode_, buffer.Text());
    entry.contentGeneration = buffer.ContentGeneration();
    entry.modeName          = mode_.name;
    embeddedDocumentCacheByBuffer_.insert_or_assign(&buffer, std::move(entry));
}

void BufferView::EnsureBlameGutterCache() const {
    text::Buffer&                buffer = activeBuffer_.Get();
    const bufferview::CacheStamp stamp  = bufferview::CacheStamp::For(&buffer, {buffer.ContentGeneration()});
    if (blameGutterCacheStamp_.Matches(stamp)) {
        return; // still valid for this buffer/content -- nothing to do (see this method's own header comment)
    }
    // Either the active buffer changed, or its content did since blame was
    // last populated -- either way, blameLineInfo_ no longer corresponds to
    // real line attribution. Clear it rather than trying to resynthesize
    // it; a fresh vcs-show-blame call is what repopulates it.
    blameLineInfo_.clear();
    blameGutterCacheStamp_ = stamp;
}

bool BufferView::BlameGutterActive() const {
    return !blameLineInfo_.empty();
}

void BufferView::ClampPointToNarrowing() {
    text::Buffer& buffer = activeBuffer_.Get();
    if (!buffer.IsNarrowed()) {
        return;
    }
    const auto [start, end] = buffer.NarrowedRange();
    // end is exclusive (the excluded next line's own start byte, or
    // ByteLength() if there is none) -- allowing point to sit exactly at
    // end would let it rest at that excluded line's own start, which
    // Content().ByteOffsetToLine (and so the mode line's own L: indicator)
    // correctly, if confusingly, reports as *being on* the excluded line --
    // a real, confirmed-via-manual-pty-testing bug, not a hypothetical one.
    // The largest valid point is one byte before end: the end of the
    // narrowed range's own last line, right before its trailing newline,
    // matching Buffer::NarrowToRegion's own identical clamp.
    const std::size_t maxPoint = end > start ? end - 1 : start;
    const std::size_t point    = buffer.Point();
    if (point < start || point > maxPoint) {
        buffer.SetPoint(std::clamp(point, start, maxPoint));
    }
}

bufferview::GutterLayout BufferView::ComputeGutterLayout(std::size_t totalLines) const {
    bufferview::GutterLayout layout;

    // The fold region is a fixed kMaxFoldDepthColumns-wide reservation rather
    // than one that grows with how deep the visible content nests, so the
    // gutter's width never shifts while scrolling past a deeply nested region.
    // The symbol column, unlike fold, is data-driven -- see
    // GutterModel::SymbolGutterActive.
    layout.dapWidth      = DapGutterActive() ? kDapWidth : 0;
    layout.diffWidth     = DiffGutterActive() ? kDiffWidth : 0;
    layout.testWidth     = gutters_.TestGutterActive() ? kTestWidth : 0;
    layout.coverageWidth = gutters_.CoverageGutterActive() ? kCoverageWidth : 0;
    layout.symbolWidth   = gutters_.SymbolGutterActive() ? kSymbolWidth : 0;
    layout.foldWidth     = gutters_.FoldGutterActive() ? kMaxFoldDepthColumns : 0;
    layout.blameWidth    = BlameGutterActive() ? kBlameWidth : 0;
    // The digits and both surrounding gaps collapse to nothing together.
    layout.lineNumberGap = LineNumberGutterActive() ? kLineNumberGap : 0;
    layout.digits        = LineNumberGutterActive() ? std::to_string(totalLines).size() : 0;

    // Left to right; status and diagnostic are the two always-reserved columns.
    layout.diffStart       = layout.dapWidth;
    layout.statusStart     = layout.diffStart + layout.diffWidth;
    layout.diagnosticStart = layout.statusStart + kStatusWidth;
    layout.digitsStart     = layout.diagnosticStart + kDiagnosticWidth + layout.lineNumberGap;
    layout.testStart       = layout.digitsStart + layout.digits + layout.lineNumberGap;
    layout.coverageStart   = layout.testStart + layout.testWidth;
    layout.symbolStart     = layout.coverageStart + layout.coverageWidth;
    layout.foldStart       = layout.symbolStart + layout.symbolWidth;
    layout.blameStart      = layout.foldStart + layout.foldWidth;
    layout.totalWidth      = layout.blameStart + layout.blameWidth;
    return layout;
}

std::size_t BufferView::GutterWidth() const {
    return ComputeGutterLayout(activeBuffer_.Get().Content().LineCount()).totalWidth;
}

std::size_t BufferView::TestGutterColumnStart() const {
    return ComputeGutterLayout(activeBuffer_.Get().Content().LineCount()).testStart;
}

bool BufferView::DiffGutterActive() const {
    return !diffLineKinds_.empty();
}

bool BufferView::LineNumberGutterActive() const {
    return editor::multibuffer::MultibufferIndexFor(activeBuffer_.Get()) == nullptr;
}

bool BufferView::DapGutterActive() const {
    if (dapManager_ == nullptr) {
        return false;
    }
    EnsureDapPathKey();
    if (dapPathKey_.empty()) {
        return false; // a pathless buffer can't hold breakpoints or be stopped in
    }
    if (!dapManager_->BreakpointLinesForKey(dapPathKey_).empty()) {
        return true;
    }
    const auto stop = dapManager_->CurrentStopKeyAndLine();
    return stop && stop->first == dapPathKey_;
}

void BufferView::EnsureDapPathKey() const {
    const text::Buffer& buffer = activeBuffer_.Get();
    if (dapPathKeyBuffer_ == &buffer && dapPathKeyRawPath_ == buffer.Path()) {
        return;
    }
    dapPathKeyBuffer_  = &buffer;
    dapPathKeyRawPath_ = buffer.Path();
    dapPathKey_        = buffer.Path() ? editor::dap::Manager::NormalizePathKey(*buffer.Path()) : std::string();
}

} // namespace ned::ui
