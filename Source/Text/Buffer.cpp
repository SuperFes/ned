#include "Buffer.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string_view>
#include <system_error>

#include "BinaryDetect.h"
#include "Grapheme.h"
#include "ThreeWayMerge.h"

namespace ned::text {

namespace {
    constexpr std::string_view kUtf8Bom = "\xEF\xBB\xBF";

    // ASCII alphanumeric + underscore -- deliberately not Unicode-aware (see
    // MoveForwardWord/MoveBackwardWord's doc comment in Buffer.h).
    bool IsWordCodepoint(char32_t codepoint) {
        return (codepoint >= U'a' && codepoint <= U'z') || (codepoint >= U'A' && codepoint <= U'Z') ||
               (codepoint >= U'0' && codepoint <= U'9') || codepoint == U'_';
    }

    // See MoveForwardSentence/MoveBackwardSentence's own doc comment in
    // Buffer.h for why this is a plain ASCII punctuation set.
    bool IsSentenceEndCodepoint(char32_t codepoint) {
        return codepoint == U'.' || codepoint == U'!' || codepoint == U'?';
    }

    bool IsSpaceOrNewlineCodepoint(char32_t codepoint) {
        return codepoint == U' ' || codepoint == U'\t' || codepoint == U'\n' || codepoint == U'\r';
    }

    // See VisualColumnForByteOffset's own doc comment in Buffer.h for why
    // this exists -- comfortably wider than any real terminal (even an
    // extreme ultra-wide setup), so every realistic file's tab-aware
    // goal-column tracking is exact; only bounds the walk for a
    // pathologically long single line. Each step calls Rope::CodepointAt, an
    // O(log document size) tree descent, not a free array index, so this
    // constant also caps the real per-call cost of landing a huge
    // carried-over goal column back onto an equally huge line -- a cost an
    // earlier version of this fix under-counted at 4096, regressing the
    // "hold next-line/previous-line" shape of the [Performance]
    // vertical-motion test to multiple seconds before it was caught.
    constexpr std::size_t kMaxTabAwareColumnScan = 512;

    // status-gutter unsaved-change-indicator follow-up: merges [start, end)
    // into ranges, which must stay sorted by .first -- a plain linear scan
    // for the merge point, not a binary search, matching this codebase's
    // "prove it before optimizing" discipline: real edits almost always
    // touch the same small handful of ranges (typing extends the one range
    // already there), so this list stays tiny in practice; revisit only if
    // a real [Performance] test says otherwise.
    void MergeUnsavedRange(std::vector<std::pair<std::size_t, std::size_t>>& ranges, std::size_t start,
                           std::size_t end) {
        auto it = ranges.begin();
        while (it != ranges.end() && it->second < start) {
            ++it;
        }
        std::size_t mergedStart = start;
        std::size_t mergedEnd   = end;
        auto        mergeBegin  = it;
        while (it != ranges.end() && it->first <= mergedEnd) {
            mergedStart = std::min(mergedStart, it->first);
            mergedEnd   = std::max(mergedEnd, it->second);
            ++it;
        }
        ranges.erase(mergeBegin, it);
        ranges.insert(mergeBegin, {mergedStart, mergedEnd});
    }

    // Undo/Redo restore a full prior Rope snapshot rather than replaying a
    // single insert/delete, so there's no edit-site offset/length already
    // in hand the way InsertAtPoint/DeleteRange have -- this recovers one
    // via a common-prefix/common-suffix scan, the standard cheap
    // approximation of a real diff (a single O(n) two-pointer pass, not a
    // full LCS diff). Exact for the overwhelmingly common case an
    // Undo()/Redo() call actually represents (undoing/redoing one coalesced
    // edit run); a pathological case with multiple disjoint changes between
    // the two snapshots would report one range spanning all of them rather
    // than several -- an accepted over-approximation, not a correctness
    // bug (the true edit sites are always a subset of the reported range).
    // Returns nullopt if the two are byte-identical (e.g. undoing straight
    // back to a state reached by pure point/mark motion, no real content
    // change). Reports both the old-text span that was effectively
    // "deleted" and the new-text span that was effectively "inserted" --
    // together the standard way to express any text replacement, matching
    // MarkUnsavedRangeDeleted/MarkUnsavedRangeInserted's own respective
    // parameter shapes so a caller can just feed this straight into both.
    struct ChangedSpan {
        std::size_t oldStart, oldEnd;
        std::size_t newStart, newEnd;
    };

    std::optional<ChangedSpan> ChangedByteRange(std::string_view oldText, std::string_view newText) {
        if (oldText == newText) {
            return std::nullopt;
        }
        const std::size_t maxCommon = std::min(oldText.size(), newText.size());
        std::size_t       prefix    = 0;
        while (prefix < maxCommon && oldText[prefix] == newText[prefix]) {
            ++prefix;
        }
        const std::size_t maxSuffix = maxCommon - prefix;
        std::size_t       suffix    = 0;
        while (suffix < maxSuffix && oldText[oldText.size() - 1 - suffix] == newText[newText.size() - 1 - suffix]) {
            ++suffix;
        }
        return ChangedSpan{prefix, oldText.size() - suffix, prefix, newText.size() - suffix};
    }
} // namespace

Buffer::Buffer(std::string name, Rope initialContent) : Name_(std::move(name)),
                                                        Rope_(initialContent),
                                                        UndoTree_(std::move(initialContent)),
                                                        SavedSnapshot_(Rope_) {
}

Buffer Buffer::FromFile(const std::filesystem::path& path, bool allowBinary) {
    // large-file-async-load follow-up: checked before any sizing/reading --
    // decoding arbitrary binary content as UTF-8 text is meaningless, and
    // this is meant to be cheap-fail-fast rather than paying for a
    // potentially huge read first. Every existing caller of
    // BufferList::OpenFile/OpenOrCreateFile already wraps the call in a
    // try/catch for exactly this kind of failure, so this needed no call
    // site changes. open-binary-anyway follow-up: allowBinary is the
    // explicit, caller-opted-in escape hatch -- see BinaryFileError's own
    // doc comment.
    if (!allowBinary && LooksBinary(path)) {
        throw BinaryFileError("ned: refusing to open binary file as text: " + path.string());
    }

    // external-modification-safety follow-up: stat the timestamp *before*
    // reading -- if the file changes between this stat and the read below,
    // a later ExternallyModified() check sees a differing timestamp and
    // flags it, whereas stat-after-read would silently absorb that write.
    std::error_code                       timestampError;
    const std::filesystem::file_time_type diskTime = std::filesystem::last_write_time(path, timestampError);

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("ned: cannot open file for reading: " + path.string());
    }

    std::uintmax_t size;
    try {
        size = std::filesystem::file_size(path);
    }
    catch (const std::filesystem::filesystem_error&) {
        throw std::runtime_error("ned: cannot determine size of file: " + path.string());
    }

    // Bulk read rather than istreambuf_iterator's byte-at-a-time extraction.
    std::string content(static_cast<std::size_t>(size), '\0');
    file.read(content.data(), static_cast<std::streamsize>(size));
    if (file.bad()) {
        throw std::runtime_error("ned: error reading file: " + path.string());
    }
    content.resize(static_cast<std::size_t>(file.gcount())); // handles a short read (e.g. file shrank concurrently)

    // Ned assumes UTF-8/ASCII content -- no charset auto-detection, matching
    // most modern editors' default -- so a leading BOM is the one
    // encoding-related artifact worth stripping explicitly.
    if (content.starts_with(kUtf8Bom)) {
        content.erase(0, kUtf8Bom.size());
    }

    Buffer buffer(path.filename().string(), Rope(content));
    buffer.Path_ = path;
    if (!timestampError) {
        buffer.DiskTimestamp_ = diskTime;
    }
    return buffer;
}

Buffer Buffer::NewFile(std::filesystem::path path) {
    Buffer buffer(path.filename().string());
    buffer.Path_ = std::move(path);
    return buffer;
}

void Buffer::SaveToFile(const std::filesystem::path& path, bool ensureFinalNewline, bool trimTrailingWhitespace) {
    // Write to a sibling temp file and rename over the target so a failure
    // partway through (e.g. disk full) can't leave the original truncated or
    // corrupted -- std::filesystem::rename is atomic on POSIX when both
    // paths are on the same filesystem, which a sibling file guarantees.
    const std::filesystem::path tempPath = path.string() + ".ned-tmp";

    {
        std::ofstream file(tempPath, std::ios::binary | std::ios::trunc);
        if (!file) {
            throw std::runtime_error("ned: cannot open file for writing: " + tempPath.string());
        }

        std::string content = Rope_.ToString();
        // trim-on-save follow-up: strips trailing spaces/tabs from every
        // line, then collapses any run of trailing blank lines down to
        // nothing -- ensureFinalNewline below is what puts exactly one '\n'
        // back if the caller still wants one. Disk-only, same reasoning as
        // ensureFinalNewline itself: only this local copy is touched, never
        // Rope_ (see Editor/TrimOnSave.h).
        if (trimTrailingWhitespace && !content.empty()) {
            std::string trimmed;
            trimmed.reserve(content.size());
            std::size_t lineStart = 0;
            for (std::size_t i = 0; i <= content.size(); ++i) {
                if (i == content.size() || content[i] == '\n') {
                    std::size_t lineEnd = i;
                    while (lineEnd > lineStart && (content[lineEnd - 1] == ' ' || content[lineEnd - 1] == '\t')) {
                        --lineEnd;
                    }
                    trimmed.append(content, lineStart, lineEnd - lineStart);
                    if (i < content.size()) {
                        trimmed.push_back('\n');
                    }
                    lineStart = i + 1;
                }
            }
            while (!trimmed.empty() && trimmed.back() == '\n') {
                trimmed.pop_back();
            }
            content = std::move(trimmed);
        }
        // An empty buffer stays empty (not turned into a bare "\n") -- and
        // Rope_ itself is never touched, only this local copy that's about
        // to be written; see the ensureFinalNewline doc comment on the
        // header for why that's deliberate.
        if (ensureFinalNewline && !content.empty() && content.back() != '\n') {
            content.push_back('\n');
        }
        file.write(content.data(), static_cast<std::streamsize>(content.size()));

        if (!file) {
            file.close();
            std::filesystem::remove(tempPath);
            throw std::runtime_error("ned: error writing file: " + tempPath.string());
        }
    } // closed here, so its contents are flushed before the rename below

    std::error_code ec;
    std::filesystem::rename(tempPath, path, ec);
    if (ec) {
        std::filesystem::remove(tempPath);
        throw std::runtime_error("ned: cannot save file: " + path.string() + " (" + ec.message() + ")");
    }

    Path_          = path;
    SavedSnapshot_ = Rope_;
    UnsavedChangeRanges_.clear();
    ++UnsavedChangeGeneration_;
    CaptureDiskTimestamp();
}

void Buffer::Save(bool ensureFinalNewline, bool trimTrailingWhitespace) {
    if (!Path_) {
        throw std::runtime_error("ned: buffer \"" + Name_ + "\" has no associated file path");
    }
    SaveToFile(*Path_, ensureFinalNewline, trimTrailingWhitespace);
}

const std::string& Buffer::Name() const {
    return Name_;
}

void Buffer::Rename(std::string name) {
    Name_ = std::move(name);
}

const std::optional<std::filesystem::path>& Buffer::Path() const {
    return Path_;
}

void Buffer::SetPath(std::filesystem::path path) {
    Path_ = std::move(path);
}

const Rope& Buffer::Content() const {
    return Rope_;
}

std::string Buffer::Text() const {
    return Rope_.ToString();
}

std::size_t Buffer::Size() const {
    return Rope_.ByteLength();
}

bool Buffer::ReadOnly() const {
    return ReadOnly_ || Loading_;
}

void Buffer::SetReadOnly(bool readOnly) {
    ReadOnly_ = readOnly;
}

bool Buffer::IsLoading() const {
    return Loading_;
}

void Buffer::MarkLoading() {
    Loading_ = true;
}

void Buffer::SetLoadProgress(std::shared_ptr<LoadProgress> progress) {
    LoadProgress_ = std::move(progress);
}

const LoadProgress* Buffer::CurrentLoadProgress() const {
    return LoadProgress_.get();
}

void Buffer::ReplaceContentForLoad(Rope content) {
    Rope_ = std::move(content);
    ++ContentGeneration_;
}

void Buffer::FinishLoad(Rope content) {
    Rope_          = std::move(content);
    UndoTree_      = UndoTree(Rope_);
    SavedSnapshot_ = Rope_;
    Loading_       = false;
    LoadProgress_.reset();
    ++ContentGeneration_;
    // Stat-after-read here, unlike FromFile's stat-before -- the async
    // loader read the content on its own thread well before this call, so
    // there's no pre-read stat available to use; a write landing in that
    // window is absorbed rather than flagged, an accepted small race for
    // the async path only.
    CaptureDiskTimestamp();
}

void Buffer::CaptureDiskTimestamp() {
    DiskTimestamp_.reset();
    if (!Path_) {
        return;
    }
    std::error_code                       ec;
    const std::filesystem::file_time_type diskTime = std::filesystem::last_write_time(*Path_, ec);
    if (!ec) {
        DiskTimestamp_ = diskTime;
    }
}

bool Buffer::ExternallyModified() const {
    if (!Path_) {
        return false;
    }
    std::error_code                       ec;
    const std::filesystem::file_time_type diskTime = std::filesystem::last_write_time(*Path_, ec);
    if (ec) {
        return false; // missing/unstatable: deletion isn't supersession (a save simply recreates it)
    }
    if (!DiskTimestamp_) {
        return true; // a file appeared underneath a NewFile() buffer that never loaded one
    }
    return diskTime != *DiskTimestamp_;
}

void Buffer::Revert() {
    if (!Path_) {
        throw std::runtime_error("ned: buffer \"" + Name_ + "\" has no associated file path");
    }
    Buffer fresh = FromFile(*Path_); // throws on any read failure, leaving this buffer untouched

    Rope_  = std::move(fresh.Rope_);
    Point_ = SnapToGraphemeBoundary(Rope_, std::min(Point_, Rope_.ByteLength()));
    Mark_.reset();
    SecondaryCursors_.clear();
    NarrowedRange_.reset();
    FoldMarkers_.clear();
    ++FoldGeneration_;

    RecordOrAmendUndo(/*canAmend=*/false); // one normal, undoable step
    GoalColumn_.reset();
    ++ContentGeneration_;

    // The buffer now matches disk by definition.
    SavedSnapshot_ = Rope_;
    UnsavedChangeRanges_.clear();
    ++UnsavedChangeGeneration_;
    DiskTimestamp_ = fresh.DiskTimestamp_;
}

std::size_t Buffer::MergeExternalChanges() {
    if (!Path_) {
        throw std::runtime_error("ned: buffer \"" + Name_ + "\" has no associated file path");
    }
    Buffer fresh = FromFile(*Path_); // throws on any read failure, leaving this buffer untouched

    const std::string       base   = SavedSnapshot_.ToString();
    const std::string       ours   = Rope_.ToString();
    const std::string       theirs = fresh.Rope_.ToString();
    const text::MergeResult result = text::ThreeWayMerge(base, ours, theirs);

    Rope_  = Rope(result.mergedText);
    Point_ = SnapToGraphemeBoundary(Rope_, std::min(result.firstConflictOffset.value_or(Point_), Rope_.ByteLength()));
    Mark_.reset();
    SecondaryCursors_.clear();
    NarrowedRange_.reset();
    FoldMarkers_.clear();
    ++FoldGeneration_;

    RecordOrAmendUndo(/*canAmend=*/false); // one normal, undoable step
    GoalColumn_.reset();
    ++ContentGeneration_;

    // Unlike Revert(), the buffer does NOT match disk now -- it combines
    // local edits with the external change, one unsaved range against the
    // new baseline below (RestoreContent's own "can't precisely attribute
    // a wholesale content replacement" shape).
    UnsavedChangeRanges_.clear();
    if (Rope_.ByteLength() > 0) {
        MergeUnsavedRange(UnsavedChangeRanges_, 0, Rope_.ByteLength());
    }
    ++UnsavedChangeGeneration_;

    // The new "last synced with disk" baseline is the freshly read disk
    // content, NOT the merged result -- see this method's own doc comment
    // in Buffer.h for why.
    SavedSnapshot_ = std::move(fresh.Rope_);
    DiskTimestamp_ = fresh.DiskTimestamp_;

    return result.conflictCount;
}

void Buffer::RestoreContent(std::string_view content) {
    Rope_  = Rope(content);
    Point_ = SnapToGraphemeBoundary(Rope_, std::min(Point_, Rope_.ByteLength()));
    Mark_.reset();
    SecondaryCursors_.clear();
    NarrowedRange_.reset();
    FoldMarkers_.clear();
    ++FoldGeneration_;

    RecordOrAmendUndo(/*canAmend=*/false); // one normal, undoable step
    GoalColumn_.reset();
    ++ContentGeneration_;

    // Unlike Revert(), the buffer does NOT match disk now -- the whole
    // restored content is one unsaved range (an empty restore over a
    // nonempty snapshot is already covered by Modified()'s own zero-length
    // fallback, so nothing is marked for it here).
    UnsavedChangeRanges_.clear();
    if (Rope_.ByteLength() > 0) {
        MergeUnsavedRange(UnsavedChangeRanges_, 0, Rope_.ByteLength());
    }
    ++UnsavedChangeGeneration_;
}

bool Buffer::Modified() const {
    if (!UnsavedChangeRanges_.empty()) {
        return true;
    }
    // The one case UnsavedChangeRanges_ genuinely cannot represent: a
    // buffer deleted all the way down to nothing has no byte left
    // anywhere to mark, even with MarkUnsavedRangeDeleted's own end-of-
    // buffer fallback (which needs at least one remaining byte to fall
    // back to). Checked directly against SavedSnapshot_'s length -- still
    // O(1), not a full content comparison -- rather than left
    // unrepresented.
    return Rope_.ByteLength() == 0 && SavedSnapshot_.ByteLength() != 0;
}

std::size_t Buffer::ContentGeneration() const {
    return ContentGeneration_;
}

std::size_t Buffer::Point() const {
    return Point_;
}

void Buffer::SetPoint(std::size_t byteOffset) {
    Point_    = SnapToGraphemeBoundary(Rope_, byteOffset);
    CanAmend_ = false;
    GoalColumn_.reset();
}

void Buffer::SetMark(std::size_t byteOffset) {
    Mark_ = SnapToGraphemeBoundary(Rope_, byteOffset);
}

void Buffer::ClearMark() {
    Mark_.reset();
}

bool Buffer::HasMark() const {
    return Mark_.has_value();
}

std::size_t Buffer::Mark() const {
    return *Mark_;
}

std::pair<std::size_t, std::size_t> Buffer::Region() const {
    const std::size_t mark = *Mark_;
    return Point_ <= mark ? std::pair{Point_, mark} : std::pair{mark, Point_};
}

void Buffer::AddCursorAt(std::size_t point, std::optional<std::size_t> mark) {
    Cursor cursor;
    cursor.point = SnapToGraphemeBoundary(Rope_, std::min(point, Rope_.ByteLength()));
    if (mark) {
        cursor.mark = SnapToGraphemeBoundary(Rope_, std::min(*mark, Rope_.ByteLength()));
    }

    if (cursor.point == Point_) {
        return; // the primary already sits here
    }
    for (const Cursor& existing : SecondaryCursors_) {
        if (existing.point == cursor.point) {
            return;
        }
    }

    SecondaryCursors_.push_back(cursor);
    NormalizeSecondaryCursors();
}

const std::vector<Buffer::Cursor>& Buffer::SecondaryCursors() const {
    return SecondaryCursors_;
}

bool Buffer::HasSecondaryCursors() const {
    return !SecondaryCursors_.empty();
}

void Buffer::ClearSecondaryCursors() {
    SecondaryCursors_.clear();
}

void Buffer::ForEachCursor(const std::function<void()>& operation) {
    // Scope guard rather than straight-line cleanup: operation is arbitrary
    // command code, and BufferView deliberately catches command exceptions
    // (a bad Janet command must not crash the editor) -- the group/
    // iteration flags must unwind correctly through that path too.
    struct IterationScope {
        Buffer& buffer;
        explicit IterationScope(Buffer& b) : buffer(b) {
            buffer.BeginUndoGroup();
            buffer.CursorIterationActive_ = true;
        }
        ~IterationScope() {
            buffer.CursorIterationActive_ = false;
            buffer.NormalizeSecondaryCursors();
            buffer.EndUndoGroup();
        }
    } scope(*this);

    // Every mutating operation relocates ALL cursors -- including the
    // currently-swapped-out ones sitting in SecondaryCursors_ -- so
    // iteration order doesn't matter for correctness and no offset here
    // ever goes stale. The swap trick is what lets operation be any
    // ordinary Point()/Mark()-based code with zero multi-cursor awareness:
    // while entry i is swapped in, the former primary is the one riding
    // along in SecondaryCursors_[i], getting the same relocation treatment.
    operation();
    for (std::size_t i = 0; i < SecondaryCursors_.size(); ++i) {
        std::swap(Point_, SecondaryCursors_[i].point);
        std::swap(Mark_, SecondaryCursors_[i].mark);
        std::swap(GoalColumn_, SecondaryCursors_[i].goalColumn);
        try {
            operation();
        }
        catch (...) {
            // Swap back before unwinding, or the primary would be left
            // holding this secondary's position through IterationScope's
            // own cleanup and beyond.
            std::swap(Point_, SecondaryCursors_[i].point);
            std::swap(Mark_, SecondaryCursors_[i].mark);
            std::swap(GoalColumn_, SecondaryCursors_[i].goalColumn);
            throw;
        }
        std::swap(Point_, SecondaryCursors_[i].point);
        std::swap(Mark_, SecondaryCursors_[i].mark);
        std::swap(GoalColumn_, SecondaryCursors_[i].goalColumn);
    }
}

void Buffer::NormalizeSecondaryCursors() {
    std::sort(SecondaryCursors_.begin(), SecondaryCursors_.end(),
              [](const Cursor& a, const Cursor& b) { return a.point < b.point; });
    SecondaryCursors_.erase(std::unique(SecondaryCursors_.begin(), SecondaryCursors_.end(),
                                        [](const Cursor& a, const Cursor& b) { return a.point == b.point; }),
                            SecondaryCursors_.end());
    std::erase_if(SecondaryCursors_, [this](const Cursor& cursor) { return cursor.point == Point_; });
}

void Buffer::BeginUndoGroup() {
    ++UndoGroupDepth_;
}

void Buffer::EndUndoGroup() {
    if (UndoGroupDepth_ == 0) {
        return; // unbalanced call -- tolerated rather than asserted
    }
    if (--UndoGroupDepth_ == 0 && UndoGroupDirty_) {
        UndoTree_.Record(Rope_);
        CanAmend_       = false;
        UndoGroupDirty_ = false;
    }
}

void Buffer::RecordOrAmendUndo(bool canAmend) {
    if (UndoGroupDepth_ > 0) {
        UndoGroupDirty_ = true;
        return;
    }
    if (canAmend && CanAmend_) {
        UndoTree_.Amend(Rope_);
        return;
    }
    UndoTree_.Record(Rope_);
    CanAmend_ = canAmend;
}

void Buffer::RelocateSecondaryCursorsForInsert(std::size_t insertOffset, std::size_t length) {
    for (Cursor& cursor : SecondaryCursors_) {
        cursor.point = RelocateForInsert(cursor.point, insertOffset, length);
        if (cursor.mark) {
            *cursor.mark = RelocateForInsert(*cursor.mark, insertOffset, length);
        }
    }
}

void Buffer::RelocateSecondaryCursorsForDelete(std::size_t rangeStart, std::size_t rangeEnd) {
    for (Cursor& cursor : SecondaryCursors_) {
        cursor.point = RelocateForDelete(cursor.point, rangeStart, rangeEnd);
        if (cursor.mark) {
            *cursor.mark = RelocateForDelete(*cursor.mark, rangeStart, rangeEnd);
        }
    }
    // A delete can collapse two cursors onto one surviving position --
    // merge immediately, EXCEPT while ForEachCursor is mid-iteration:
    // normalizing there would erase/reorder the very slots its swap
    // bookkeeping indexes into (including the swapped-out primary riding in
    // one of them), so it defers to the single normalize at its own end.
    // The cost of deferring is only that a mid-iteration collapse can run
    // the operation twice at one position before the merge -- accepted, the
    // same eventual-merge behavior Emacs' own multiple-cursors has.
    if (!CursorIterationActive_) {
        NormalizeSecondaryCursors();
    }
}

void Buffer::NarrowToRegion(std::size_t start, std::size_t end) {
    if (start > end) {
        std::swap(start, end);
    }
    start = std::min(start, Rope_.ByteLength());
    end   = std::min(end, Rope_.ByteLength());

    const std::size_t totalLines   = Rope_.LineCount();
    const std::size_t startLine    = Rope_.ByteOffsetToLine(start);
    const std::size_t snappedStart = Rope_.LineToByteOffset(startLine);

    const std::size_t endLine = Rope_.ByteOffsetToLine(end);
    const std::size_t snappedEnd =
        (endLine + 1 < totalLines) ? Rope_.LineToByteOffset(endLine + 1) : Rope_.ByteLength();

    NarrowedRange_ = std::pair{snappedStart, snappedEnd};
    // snappedEnd is exclusive (the excluded next line's own start byte, or
    // ByteLength() if there is none) -- clamping point to snappedEnd itself
    // would let it sit exactly at that excluded line's start, which
    // ByteOffsetToLine correctly (if confusingly) classifies as *being on*
    // the excluded line, not the narrowed range's own last line. The
    // largest valid point is one byte before that -- always still within
    // the last included line's own content or its trailing newline, never
    // past it, for the same reason BufferView::Paint's own lineEnd
    // computation already subtracts 1 from a next-line boundary.
    const std::size_t maxPoint = snappedEnd > snappedStart ? snappedEnd - 1 : snappedStart;
    Point_                     = std::clamp(Point_, snappedStart, maxPoint);
}

void Buffer::Widen() {
    NarrowedRange_.reset();
}

bool Buffer::IsNarrowed() const {
    return NarrowedRange_.has_value();
}

std::pair<std::size_t, std::size_t> Buffer::NarrowedRange() const {
    return *NarrowedRange_;
}

void Buffer::SetFoldMarker(std::size_t byteOffset, std::optional<FoldMarker> marker) {
    if (marker) {
        FoldMarkers_[byteOffset] = *marker;
    }
    else {
        FoldMarkers_.erase(byteOffset);
    }
    ++FoldGeneration_;
}

std::optional<Buffer::FoldMarker> Buffer::FoldMarkerAt(std::size_t byteOffset) const {
    const auto it = FoldMarkers_.find(byteOffset);
    return it != FoldMarkers_.end() ? std::optional{it->second} : std::nullopt;
}

const std::map<std::size_t, Buffer::FoldMarker>& Buffer::FoldMarkers() const {
    return FoldMarkers_;
}

std::size_t Buffer::FoldGeneration() const {
    return FoldGeneration_;
}

const std::vector<std::pair<std::size_t, std::size_t>>& Buffer::UnsavedChangeRanges() const {
    return UnsavedChangeRanges_;
}

std::size_t Buffer::UnsavedChangeGeneration() const {
    return UnsavedChangeGeneration_;
}

void Buffer::SetDiagnostics(std::vector<Diagnostic> diagnostics) {
    Diagnostics_ = std::move(diagnostics);
    ++DiagnosticsGeneration_;
}

const std::vector<Buffer::Diagnostic>& Buffer::Diagnostics() const {
    return Diagnostics_;
}

std::size_t Buffer::DiagnosticsGeneration() const {
    return DiagnosticsGeneration_;
}

std::size_t Buffer::RelocateForInsert(std::size_t offset, std::size_t insertOffset, std::size_t length) {
    return offset >= insertOffset ? offset + length : offset;
}

std::size_t Buffer::RelocateForDelete(std::size_t offset, std::size_t rangeStart, std::size_t rangeEnd) {
    if (offset >= rangeEnd) {
        return offset - (rangeEnd - rangeStart);
    }
    if (offset > rangeStart) {
        return rangeStart;
    }
    return offset;
}

void Buffer::RelocateFoldMarkersForInsert(std::size_t insertOffset, std::size_t length) {
    if (FoldMarkers_.empty()) {
        return;
    }
    std::map<std::size_t, FoldMarker> relocated;
    for (const auto& [offset, marker] : FoldMarkers_) {
        relocated.emplace(RelocateForInsert(offset, insertOffset, length), marker);
    }
    FoldMarkers_ = std::move(relocated);
    ++FoldGeneration_;
}

void Buffer::RelocateFoldMarkersForDelete(std::size_t rangeStart, std::size_t rangeEnd) {
    if (FoldMarkers_.empty()) {
        return;
    }
    std::map<std::size_t, FoldMarker> relocated;
    for (const auto& [offset, marker] : FoldMarkers_) {
        relocated.emplace(RelocateForDelete(offset, rangeStart, rangeEnd), marker);
    }
    FoldMarkers_ = std::move(relocated);
    ++FoldGeneration_;
}

void Buffer::MarkUnsavedRangeInserted(std::size_t insertOffset, std::size_t length) {
    for (auto& [start, end] : UnsavedChangeRanges_) {
        start = RelocateForInsert(start, insertOffset, length);
        end   = RelocateForInsert(end, insertOffset, length);
    }
    MergeUnsavedRange(UnsavedChangeRanges_, insertOffset, insertOffset + length);
    ++UnsavedChangeGeneration_;
}

void Buffer::MarkUnsavedRangeDeleted(std::size_t rangeStart, std::size_t rangeEnd) {
    for (auto& [start, end] : UnsavedChangeRanges_) {
        start = RelocateForDelete(start, rangeStart, rangeEnd);
        end   = RelocateForDelete(end, rangeStart, rangeEnd);
    }
    std::erase_if(UnsavedChangeRanges_, [](const auto& range) { return range.first >= range.second; });

    // A delete removes content -- there's no span left to mark, only the
    // position it collapsed to. One byte is enough for the line it maps to
    // at render time; deleting through to the end of whatever's left has
    // no byte *after* the collapse point to mark, so this falls back to
    // the byte just *before* it instead (still real, still-existing
    // content on the same line the edit happened on) rather than silently
    // recording nothing. Was silently recording nothing -- a real bug,
    // found once Modified() was unified onto UnsavedChangeRanges_ (see
    // that method's own doc comment): deleting the very last character of
    // a buffer left no marker at all, so Modified() incorrectly reported
    // false for an edit that plainly happened. Only the buffer becoming
    // completely empty (rangeStart == 0 too) has truly nothing left
    // anywhere to point a byte range at -- Modified() handles that one
    // remaining case separately, see its own doc comment.
    std::size_t markStart = rangeStart;
    std::size_t markEnd   = std::min(rangeStart + 1, Rope_.ByteLength());
    if (markEnd <= markStart && markStart > 0) {
        markStart = markStart - 1;
        markEnd   = markStart + 1;
    }
    if (markEnd > markStart) {
        MergeUnsavedRange(UnsavedChangeRanges_, markStart, markEnd);
    }
    ++UnsavedChangeGeneration_;
}

void Buffer::InsertAtPoint(std::string_view text) {
    if (ReadOnly_ || Loading_) {
        throw std::runtime_error("Buffer is read-only.");
    }
    if (text.empty()) {
        return;
    }

    const std::size_t insertOffset = Point_;
    Rope_                          = Rope_.Inserted(insertOffset, text);
    Point_                         = RelocateForInsert(Point_, insertOffset, text.size());

    if (Mark_) {
        *Mark_ = RelocateForInsert(*Mark_, insertOffset, text.size());
    }
    if (NarrowedRange_) {
        auto& [narrowStart, narrowEnd] = *NarrowedRange_;
        narrowStart                    = RelocateForInsert(narrowStart, insertOffset, text.size());
        narrowEnd                      = RelocateForInsert(narrowEnd, insertOffset, text.size());
    }
    RelocateFoldMarkersForInsert(insertOffset, text.size());
    RelocateSecondaryCursorsForInsert(insertOffset, text.size());
    MarkUnsavedRangeInserted(insertOffset, text.size());

    RecordOrAmendUndo(/*canAmend=*/true);
    GoalColumn_.reset();
    ++ContentGeneration_;
}

void Buffer::DeleteBackwardAtPoint() {
    if (ReadOnly_ || Loading_) {
        throw std::runtime_error("Buffer is read-only.");
    }
    if (Point_ == 0) {
        return;
    }

    const std::size_t start = PreviousGraphemeBoundary(Rope_, Point_);
    const std::size_t end   = Point_;
    Rope_                   = Rope_.Erased(start, end - start);

    Point_ = RelocateForDelete(Point_, start, end);
    if (Mark_) {
        *Mark_ = RelocateForDelete(*Mark_, start, end);
    }
    if (NarrowedRange_) {
        auto& [narrowStart, narrowEnd] = *NarrowedRange_;
        narrowStart                    = RelocateForDelete(narrowStart, start, end);
        narrowEnd                      = RelocateForDelete(narrowEnd, start, end);
        if (narrowStart >= narrowEnd) {
            NarrowedRange_.reset();
        }
    }
    RelocateFoldMarkersForDelete(start, end);
    RelocateSecondaryCursorsForDelete(start, end);
    MarkUnsavedRangeDeleted(start, end);

    RecordOrAmendUndo(/*canAmend=*/false);
    GoalColumn_.reset();
    ++ContentGeneration_;
}

void Buffer::DeleteForwardAtPoint() {
    if (ReadOnly_ || Loading_) {
        throw std::runtime_error("Buffer is read-only.");
    }
    if (Point_ >= Rope_.ByteLength()) {
        return;
    }

    const std::size_t start = Point_;
    const std::size_t end   = NextGraphemeBoundary(Rope_, Point_);
    Rope_                   = Rope_.Erased(start, end - start);

    Point_ = RelocateForDelete(Point_, start, end);
    if (Mark_) {
        *Mark_ = RelocateForDelete(*Mark_, start, end);
    }
    if (NarrowedRange_) {
        auto& [narrowStart, narrowEnd] = *NarrowedRange_;
        narrowStart                    = RelocateForDelete(narrowStart, start, end);
        narrowEnd                      = RelocateForDelete(narrowEnd, start, end);
        if (narrowStart >= narrowEnd) {
            NarrowedRange_.reset();
        }
    }
    RelocateFoldMarkersForDelete(start, end);
    RelocateSecondaryCursorsForDelete(start, end);
    MarkUnsavedRangeDeleted(start, end);

    RecordOrAmendUndo(/*canAmend=*/false);
    GoalColumn_.reset();
    ++ContentGeneration_;
}

std::string Buffer::DeleteRange(std::size_t byteOffset, std::size_t byteLength) {
    if (ReadOnly_ || Loading_) {
        throw std::runtime_error("Buffer is read-only.");
    }
    byteOffset = std::min(byteOffset, Rope_.ByteLength());
    byteLength = std::min(byteLength, Rope_.ByteLength() - byteOffset);

    if (byteLength == 0) {
        return {};
    }

    const std::size_t rangeEnd = byteOffset + byteLength;
    std::string       deleted  = Rope_.Substring(byteOffset, byteLength);
    Rope_                      = Rope_.Erased(byteOffset, byteLength);

    Point_ = RelocateForDelete(Point_, byteOffset, rangeEnd);
    if (Mark_) {
        *Mark_ = RelocateForDelete(*Mark_, byteOffset, rangeEnd);
    }

    if (NarrowedRange_) {
        auto& [narrowStart, narrowEnd] = *NarrowedRange_;
        narrowStart                    = RelocateForDelete(narrowStart, byteOffset, rangeEnd);
        narrowEnd                      = RelocateForDelete(narrowEnd, byteOffset, rangeEnd);
        // A delete that consumes the narrowed range's entire content
        // (narrowStart no longer strictly before narrowEnd) leaves nothing
        // meaningful to stay narrowed to -- auto-widen rather than leaving
        // the editor in a broken, everything-hidden state.
        if (narrowStart >= narrowEnd) {
            NarrowedRange_.reset();
        }
    }
    RelocateFoldMarkersForDelete(byteOffset, rangeEnd);
    RelocateSecondaryCursorsForDelete(byteOffset, rangeEnd);
    MarkUnsavedRangeDeleted(byteOffset, rangeEnd);

    RecordOrAmendUndo(/*canAmend=*/false);
    GoalColumn_.reset();
    ++ContentGeneration_;
    return deleted;
}

void Buffer::InsertAt(std::size_t byteOffset, std::string_view text) {
    if (ReadOnly_ || Loading_) {
        throw std::runtime_error("Buffer is read-only.");
    }
    InsertAtImpl(byteOffset, text);
}

void Buffer::AppendWhileReadOnly(std::string_view text) {
    if (!ReadOnly_) {
        throw std::logic_error("AppendWhileReadOnly called on a writable buffer.");
    }
    InsertAtImpl(Rope_.ByteLength(), text);
}

void Buffer::InsertAtImpl(std::size_t byteOffset, std::string_view text) {
    byteOffset = std::min(byteOffset, Rope_.ByteLength());

    if (text.empty()) {
        return;
    }

    Rope_ = Rope_.Inserted(byteOffset, text);

    Point_ = RelocateForInsert(Point_, byteOffset, text.size());
    if (Mark_) {
        *Mark_ = RelocateForInsert(*Mark_, byteOffset, text.size());
    }
    if (NarrowedRange_) {
        auto& [narrowStart, narrowEnd] = *NarrowedRange_;
        narrowStart                    = RelocateForInsert(narrowStart, byteOffset, text.size());
        narrowEnd                      = RelocateForInsert(narrowEnd, byteOffset, text.size());
    }
    RelocateFoldMarkersForInsert(byteOffset, text.size());
    RelocateSecondaryCursorsForInsert(byteOffset, text.size());
    MarkUnsavedRangeInserted(byteOffset, text.size());

    RecordOrAmendUndo(/*canAmend=*/false);
    GoalColumn_.reset();
    ++ContentGeneration_;
}

void Buffer::MoveForward() {
    Point_    = NextGraphemeBoundary(Rope_, Point_);
    CanAmend_ = false;
    GoalColumn_.reset();
}

void Buffer::MoveBackward() {
    Point_    = PreviousGraphemeBoundary(Rope_, Point_);
    CanAmend_ = false;
    GoalColumn_.reset();
}

void Buffer::MoveForwardWord() {
    const std::size_t total  = Rope_.ByteLength();
    std::size_t       offset = Point_;

    while (offset < total && !IsWordCodepoint(Rope_.CodepointAt(offset).codepoint)) {
        offset = Rope_.NextCodepointBoundary(offset);
    }
    while (offset < total && IsWordCodepoint(Rope_.CodepointAt(offset).codepoint)) {
        offset = Rope_.NextCodepointBoundary(offset);
    }

    Point_    = SnapToGraphemeBoundary(Rope_, offset);
    CanAmend_ = false;
    GoalColumn_.reset();
}

void Buffer::MoveBackwardWord() {
    std::size_t offset = Point_;

    while (offset > 0) {
        const std::size_t previous = Rope_.PreviousCodepointBoundary(offset);
        if (IsWordCodepoint(Rope_.CodepointAt(previous).codepoint)) {
            break;
        }
        offset = previous;
    }
    while (offset > 0) {
        const std::size_t previous = Rope_.PreviousCodepointBoundary(offset);
        if (!IsWordCodepoint(Rope_.CodepointAt(previous).codepoint)) {
            break;
        }
        offset = previous;
    }

    Point_    = SnapToGraphemeBoundary(Rope_, offset);
    CanAmend_ = false;
    GoalColumn_.reset();
}

void Buffer::MoveForwardSentence() {
    const std::size_t total  = Rope_.ByteLength();
    std::size_t       offset = Point_;

    while (offset < total && !IsSentenceEndCodepoint(Rope_.CodepointAt(offset).codepoint)) {
        offset = Rope_.NextCodepointBoundary(offset);
    }
    if (offset < total) {
        offset = Rope_.NextCodepointBoundary(offset); // past the sentence-ending punctuation itself
        while (offset < total && IsSpaceOrNewlineCodepoint(Rope_.CodepointAt(offset).codepoint)) {
            offset = Rope_.NextCodepointBoundary(offset);
        }
    }

    Point_    = SnapToGraphemeBoundary(Rope_, offset);
    CanAmend_ = false;
    GoalColumn_.reset();
}

void Buffer::MoveBackwardSentence() {
    std::size_t offset = Point_;

    // Skip back over whitespace directly before point -- mirrors forward's
    // own post-punctuation whitespace skip, so this can land right back
    // where a preceding MoveForwardSentence call would have stopped.
    while (offset > 0) {
        const std::size_t previous = Rope_.PreviousCodepointBoundary(offset);
        if (!IsSpaceOrNewlineCodepoint(Rope_.CodepointAt(previous).codepoint)) {
            break;
        }
        offset = previous;
    }
    // Sitting right after a sentence-ending mark (the common case just
    // after the whitespace skip above): hop back over it too, or the scan
    // below would immediately re-find that same mark and refuse to move.
    if (offset > 0) {
        const std::size_t previous = Rope_.PreviousCodepointBoundary(offset);
        if (IsSentenceEndCodepoint(Rope_.CodepointAt(previous).codepoint)) {
            offset = previous;
        }
    }
    while (offset > 0) {
        const std::size_t previous = Rope_.PreviousCodepointBoundary(offset);
        if (IsSentenceEndCodepoint(Rope_.CodepointAt(previous).codepoint)) {
            break;
        }
        offset = previous;
    }
    // offset now sits right after the previous sentence's own end mark (or
    // 0) -- skip forward over whitespace to land on the first real
    // character, matching MoveForwardSentence's own landing spot.
    while (offset < Rope_.ByteLength() && IsSpaceOrNewlineCodepoint(Rope_.CodepointAt(offset).codepoint)) {
        offset = Rope_.NextCodepointBoundary(offset);
    }

    Point_    = SnapToGraphemeBoundary(Rope_, offset);
    CanAmend_ = false;
    GoalColumn_.reset();
}

std::size_t Buffer::ByteOffsetForLineAndColumn(std::size_t line, std::size_t column, std::size_t tabWidth) const {
    const std::size_t totalLines = Rope_.LineCount();
    line                         = std::min(line, totalLines - 1);

    const std::size_t lineStart = Rope_.LineToByteOffset(line);
    const std::size_t lineEnd   = (line + 1 < totalLines) ? Rope_.LineToByteOffset(line + 1) - 1 : Rope_.ByteLength();

    if (tabWidth <= 1) {
        const std::size_t lineStartCodepoint = Rope_.ByteOffsetToCodepointOffset(lineStart);
        const std::size_t lineLength         = Rope_.ByteOffsetToCodepointOffset(lineEnd) - lineStartCodepoint;

        const std::size_t landingCodepoint = lineStartCodepoint + std::min(column, lineLength);
        return Rope_.CodepointOffsetToByteOffset(landingCodepoint);
    }

    // In the common case this is bounded by `column` itself -- the walk
    // stops the moment the accumulated visual column reaches it. But
    // `column` isn't always screen-width-small: MoveToLine can carry over a
    // GoalColumn_ approximated from a pathologically long *other* line (see
    // VisualColumnForByteOffset), and landing that huge column on an equally
    // long target line would walk the whole thing. kMaxTabAwareColumnScan
    // caps that the same way, falling back to plain codepoint arithmetic
    // (clamped to the line's actual end) for the remainder.
    std::size_t offset       = lineStart;
    std::size_t visualColumn = 0;
    std::size_t steps        = 0;
    while (offset < lineEnd && visualColumn < column) {
        if (steps >= kMaxTabAwareColumnScan) {
            const std::size_t remainingColumns = column - visualColumn;
            const std::size_t lineEndCodepoint = Rope_.ByteOffsetToCodepointOffset(lineEnd);
            const std::size_t landingCodepoint = std::min(Rope_.ByteOffsetToCodepointOffset(offset) + remainingColumns,
                                                          lineEndCodepoint);
            return Rope_.CodepointOffsetToByteOffset(landingCodepoint);
        }
        const auto decoded = Rope_.CodepointAt(offset);
        visualColumn += (decoded.codepoint == U'\t') ? tabWidth : 1;
        offset += decoded.byteLength;
        ++steps;
    }
    return offset;
}

std::size_t Buffer::VisualColumnForByteOffset(std::size_t lineStart, std::size_t byteOffset,
                                              std::size_t tabWidth) const {
    if (tabWidth <= 1) {
        return Rope_.ByteOffsetToCodepointOffset(byteOffset) - Rope_.ByteOffsetToCodepointOffset(lineStart);
    }

    std::size_t offset = lineStart;
    std::size_t column = 0;
    std::size_t steps  = 0;
    while (offset < byteOffset) {
        if (steps >= kMaxTabAwareColumnScan) {
            // Bail out to a plain codepoint-distance approximation for the
            // remainder -- see kMaxTabAwareColumnScan's own comment.
            return column + (Rope_.ByteOffsetToCodepointOffset(byteOffset) - Rope_.ByteOffsetToCodepointOffset(offset));
        }
        const auto decoded = Rope_.CodepointAt(offset);
        column += (decoded.codepoint == U'\t') ? tabWidth : 1;
        offset += decoded.byteLength;
        ++steps;
    }
    return column;
}

void Buffer::MoveToLine(std::size_t targetLine, std::size_t tabWidth) {
    const std::size_t currentLineStart = Rope_.LineToByteOffset(Rope_.ByteOffsetToLine(Point_));
    const std::size_t desiredColumn    = GoalColumn_.value_or(VisualColumnForByteOffset(currentLineStart, Point_, tabWidth));

    const std::size_t landingByte = ByteOffsetForLineAndColumn(targetLine, desiredColumn, tabWidth);

    Point_      = SnapToGraphemeBoundary(Rope_, landingByte);
    GoalColumn_ = desiredColumn; // the un-clamped goal, not necessarily where we landed
    CanAmend_   = false;
}

void Buffer::MoveDownLines(std::size_t count, std::size_t tabWidth) {
    const std::size_t currentLine = Rope_.ByteOffsetToLine(Point_);
    const std::size_t lastLine    = Rope_.LineCount() - 1;
    if (currentLine == lastLine) {
        return; // already on the last line -- true no-op, regardless of any stale goal column
    }
    // Clamped rather than a plain currentLine + count: a page-down whose
    // count overshoots the end of a short buffer should still land on the
    // last line instead of doing nothing.
    MoveToLine(std::min(currentLine + count, lastLine), tabWidth);
}

void Buffer::MoveUpLines(std::size_t count, std::size_t tabWidth) {
    const std::size_t currentLine = Rope_.ByteOffsetToLine(Point_);
    if (currentLine == 0) {
        return; // already on the first line -- true no-op
    }
    MoveToLine(count > currentLine ? 0 : currentLine - count, tabWidth);
}

void Buffer::MoveToNextLine(std::size_t tabWidth) {
    MoveDownLines(1, tabWidth);
}

void Buffer::MoveToPreviousLine(std::size_t tabWidth) {
    MoveUpLines(1, tabWidth);
}

bool Buffer::CanUndo() const {
    return UndoTree_.CanUndo();
}

bool Buffer::CanRedo() const {
    return UndoTree_.CanRedo();
}

void Buffer::ClampCursorsToContent() {
    Point_ = SnapToGraphemeBoundary(Rope_, std::min(Point_, Rope_.ByteLength()));
    if (Mark_) {
        Mark_ = SnapToGraphemeBoundary(Rope_, std::min(*Mark_, Rope_.ByteLength()));
    }
    if (NarrowedRange_) {
        // Undo/redo can swap in content of a completely different length --
        // clamp the range into whatever's now valid, and auto-widen if it
        // would otherwise become degenerate (start no longer strictly
        // before end), the same rule DeleteRange's own narrowing-tracking
        // uses.
        auto& [narrowStart, narrowEnd] = *NarrowedRange_;
        narrowStart                    = std::min(narrowStart, Rope_.ByteLength());
        narrowEnd                      = std::min(narrowEnd, Rope_.ByteLength());
        if (narrowStart >= narrowEnd) {
            NarrowedRange_.reset();
        }
    }
    // Undo/redo can swap in content shorter than some fold-marker offsets
    // were relocated against -- unlike Point_/Mark_/NarrowedRange_, there's
    // no single sensible position to clamp a marker down to (it would
    // collide arbitrarily with whatever's left), so a marker past the new
    // end is simply dropped rather than clamped.
    if (!FoldMarkers_.empty()) {
        const std::size_t length = Rope_.ByteLength();
        std::erase_if(FoldMarkers_, [length](const auto& entry) { return entry.first > length; });
    }
}

void Buffer::Undo() {
    if (!UndoTree_.CanUndo()) {
        return;
    }
    const std::string oldText = Rope_.ToString();
    UndoTree_.Undo();
    Rope_ = UndoTree_.Current();
    ClearSecondaryCursors(); // v1 decision -- see AddCursorAt's doc comment
    ClampCursorsToContent();
    CanAmend_ = false;
    GoalColumn_.reset();
    ++ContentGeneration_;
    UpdateUnsavedRangesForRestore(oldText);
}

void Buffer::Redo() {
    if (!UndoTree_.CanRedo()) {
        return;
    }
    const std::string oldText = Rope_.ToString();
    UndoTree_.Redo();
    Rope_ = UndoTree_.Current();
    ClearSecondaryCursors(); // v1 decision -- see AddCursorAt's doc comment
    ClampCursorsToContent();
    CanAmend_ = false;
    GoalColumn_.reset();
    ++ContentGeneration_;
    UpdateUnsavedRangesForRestore(oldText);
}

void Buffer::UpdateUnsavedRangesForRestore(const std::string& oldText) {
    const std::string newText = Rope_.ToString();

    // Landed back on exactly the last-saved content, regardless of the
    // path taken to get there (this Undo()/Redo() step, or several
    // compounded with earlier ones) -- no unsaved change at all, full
    // stop, not just "nothing changed in this specific step." Found to be
    // necessary via live testing: the diff-against-oldText path below is
    // exact for *this* step, but an edit undone anywhere except the very
    // end of the buffer still left a real, technically-accurate 1-byte
    // marker at the edit site even once content matched disk again --
    // correct in isolation, misleading in practice (still showed as an
    // unsaved change after undoing the exact edit that caused it).
    if (newText == SavedSnapshot_.ToString()) {
        if (!UnsavedChangeRanges_.empty()) {
            UnsavedChangeRanges_.clear();
            ++UnsavedChangeGeneration_;
        }
        return;
    }

    // Undoing/redoing restores a full prior Rope snapshot rather than
    // replaying a single insert/delete, so there's no edit-site
    // offset/length already in hand -- ChangedByteRange recovers one via a
    // common-prefix/suffix diff against the just-replaced content, then
    // this composes onto the exact same MarkUnsavedRangeDeleted+
    // MarkUnsavedRangeInserted machinery every real edit already goes
    // through (delete-then-insert is the standard way to express a
    // replacement), including relocating any other still-unsaved ranges
    // through it. Was a coarse "mark the whole buffer dirty" fallback --
    // found to be a real, user-visible bug during manual testing (undoing
    // a single trivial edit lit up the entire gutter as changed), not just
    // an approximation worth tightening later.
    if (const auto span = ChangedByteRange(oldText, newText)) {
        // Only the non-empty side of the replacement -- MarkUnsavedRange*
        // each unconditionally record a span (MarkUnsavedRangeInserted's
        // own merge call has no empty-length guard the way InsertAtPoint's
        // own early return gives it in the normal edit path), so a
        // zero-width side (a pure insert has no "deleted" span and vice
        // versa) would otherwise leave a stray empty entry in
        // UnsavedChangeRanges_.
        if (span->oldEnd > span->oldStart) {
            MarkUnsavedRangeDeleted(span->oldStart, span->oldEnd);
        }
        if (span->newEnd > span->newStart) {
            MarkUnsavedRangeInserted(span->newStart, span->newEnd - span->newStart);
        }
    }
}

} // namespace ned::text
