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
    text::Buffer& buffer = activeBuffer_.Get();
    const bufferview::CacheStamp stamp = bufferview::CacheStamp::For(&buffer, {buffer.ContentGeneration()});
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



std::size_t BufferView::GutterWidth() const {
    const std::size_t totalLines = activeBuffer_.Get().Content().LineCount();
    // status/line-number-spacing follow-up (LSP client follow-up: gained a
    // second, dedicated diagnostic column -- see kDiagnosticWidth's own doc
    // comment): [status][diagnostic][gap][digits][gap][symbol][fold], left
    // to right -- status and diagnostic are always reserved; the fold region
    // (generic-code-folding / depth-aware-fold-gutter follow-ups) only when
    // a mode has a real fold query and the feature is enabled, a fixed
    // kMaxFoldDepthColumns-wide reservation (not one that grows with how
    // deep the currently-visible content happens to nest -- an explicit
    // user choice, so the gutter's own width never jumps around while
    // scrolling past a deeply nested region). symbol (gutter-symbol-kind
    // follow-up), unlike fold, IS data-driven -- see SymbolGutterActive's
    // own doc comment for why.
    const std::size_t foldColumn     = gutters_.FoldGutterActive() ? kMaxFoldDepthColumns : 0;
    const std::size_t blameColumn    = BlameGutterActive() ? kBlameWidth : 0;
    const std::size_t diffColumn     = DiffGutterActive() ? kDiffWidth : 0;
    const std::size_t dapColumn      = DapGutterActive() ? kDapWidth : 0;
    const std::size_t symbolColumn   = gutters_.SymbolGutterActive() ? kSymbolWidth : 0;
    const std::size_t testColumn     = gutters_.TestGutterActive() ? kTestWidth : 0;
    const std::size_t coverageColumn = gutters_.CoverageGutterActive() ? kCoverageWidth : 0;
    // Multibuffers follow-up: the line-number digits + both surrounding
    // gaps collapse to zero width together when LineNumberGutterActive()
    // is false -- see its own doc comment.
    const std::size_t lineNumberColumn =
        LineNumberGutterActive() ? (kLineNumberGap + std::to_string(totalLines).size() + kLineNumberGap) : 0;
    return dapColumn + diffColumn + kStatusWidth + kDiagnosticWidth + lineNumberColumn + testColumn + coverageColumn +
           symbolColumn + foldColumn + blameColumn;
}



std::size_t BufferView::TestGutterColumnStart() const {
    // Mirrors Paint()'s own left-to-right column sum
    // ([dap][diff][status][diagnostic][gap][digits][gap][test]) rather than
    // the fold-click's subtract-from-the-right trick -- that one works only
    // because fold and blame are the two rightmost regions, which the test
    // column is not.
    const std::size_t dapColumnWidth     = DapGutterActive() ? kDapWidth : 0;
    const std::size_t diffColumnWidth    = DiffGutterActive() ? kDiffWidth : 0;
    const std::size_t lineNumberGapWidth = LineNumberGutterActive() ? kLineNumberGap : 0;
    const std::size_t gutterDigits =
        LineNumberGutterActive() ? std::to_string(activeBuffer_.Get().Content().LineCount()).size() : 0;
    return dapColumnWidth + diffColumnWidth + kStatusWidth + kDiagnosticWidth + lineNumberGapWidth + gutterDigits +
           lineNumberGapWidth;
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
    dapPathKey_        = buffer.Path() ? editor::dap::DapManager::NormalizePathKey(*buffer.Path()) : std::string();
}












} // namespace ned::ui
