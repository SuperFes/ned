#include "Buffer.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string_view>
#include <system_error>

#include "BinaryDetect.h"
#include "BufferList.h"
#include "DiskSpace.h"
#include "Grapheme.h"
#include "LineEnding.h"
#include "PieceTable.h"
#include "PieceTableStorage.h"
#include "RopeStorage.h"
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

    // disk-space-safety follow-up: a plain "N.N GiB"/"N.N MiB" formatter for
    // ReadOnlyReason()/SaveToFile's disk-space error messages -- no existing
    // helper for this anywhere in the codebase (grepped first).
    std::string FormatBytesHuman(std::uintmax_t bytes) {
        constexpr double kGiB = 1024.0 * 1024.0 * 1024.0;
        constexpr double kMiB = 1024.0 * 1024.0;
        char             buffer[32];
        if (bytes >= static_cast<std::uintmax_t>(kGiB)) {
            std::snprintf(buffer, sizeof(buffer), "%.1f GiB", static_cast<double>(bytes) / kGiB);
        } else {
            std::snprintf(buffer, sizeof(buffer), "%.1f MiB", static_cast<double>(bytes) / kMiB);
        }
        return buffer;
    }

    // huge-file-editing follow-up (streaming save): reproduces SaveToFile's
    // trim-trailing-whitespace / ensure-final-newline / line-ending-
    // expansion pipeline as a single forward streaming pass over content
    // fed in arbitrary-sized chunks (ITextStorage::ForEachChunk's contract
    // -- a chunk boundary carries no semantic meaning, a line or a run of
    // trailing whitespace can span one), instead of materializing the whole
    // document into one string first the way SaveToFile's non-huge path
    // still does (deliberately left alone -- see SaveToFile's own comment
    // on why). Byte-identical output to that whole-string algorithm --
    // verified directly, see BufferSaveEquivalenceTest.cpp, not just
    // reasoned about.
    //
    // Trim, restated as a streaming state machine: a trailing space/tab run
    // within the CURRENT line is held in pendingWhitespace_ (bounded by
    // that run's own length, not file size) until either a non-whitespace
    // byte on the same line arrives (not trailing after all -- flush it) or
    // the line ends (confirmed trailing -- discard it, matching every line
    // getting its own trailing whitespace stripped unconditionally). A '\n'
    // is held as one unit of pendingNewlineCount_ until either more real
    // (post-trim) content follows later (flush that many '\n's first,
    // confirmed real separators) or the stream ends with none flushed
    // (confirmed all trailing -- discard them all), reproducing the
    // original algorithm's separate "strip every trailing '\n'" pass with
    // no second pass or full-content buffer needed.
    class StreamingSaveWriter {
      public:
        StreamingSaveWriter(std::ofstream& file, LineEnding ending, bool trim, bool ensureFinalNewline)
            : file_(file), ending_(ending), trim_(trim), ensureFinalNewline_(ensureFinalNewline) {}

        void operator()(std::string_view chunk) {
            for (char c : chunk) {
                Feed(c);
            }
        }

        // Call once after every chunk has been fed. Applies
        // ensureFinalNewline and flushes any buffered output -- does NOT
        // check the stream for a write failure itself; the caller checks
        // `file` once after this returns, same as SaveToFile's non-huge
        // path already does after its own single write.
        void Finish() {
            if (ensureFinalNewline_ && wroteAnything_ && !endsWithNewline_) {
                WriteNewline();
            }
            FlushBuffer();
        }

      private:
        void Feed(char c) {
            if (!trim_) {
                if (c == '\n') {
                    WriteNewline();
                } else {
                    WriteByte(c);
                }
                return;
            }

            if (c == ' ' || c == '\t') {
                pendingWhitespace_.push_back(c);
            } else if (c == '\n') {
                pendingWhitespace_.clear(); // trailing on this line -- discard
                ++pendingNewlineCount_;
            } else {
                for (std::size_t i = 0; i < pendingNewlineCount_; ++i) {
                    WriteNewline();
                }
                pendingNewlineCount_ = 0;
                for (char w : pendingWhitespace_) {
                    WriteByte(w);
                }
                pendingWhitespace_.clear();
                WriteByte(c);
            }
        }

        void WriteByte(char c) {
            outBuffer_.push_back(c);
            wroteAnything_   = true;
            endsWithNewline_ = false;
            MaybeFlush();
        }

        void WriteNewline() {
            if (ending_ == LineEnding::CRLF) {
                outBuffer_.append("\r\n");
            } else if (ending_ == LineEnding::CR) {
                outBuffer_.push_back('\r');
            } else {
                outBuffer_.push_back('\n');
            }
            wroteAnything_   = true;
            endsWithNewline_ = true;
            MaybeFlush();
        }

        void MaybeFlush() {
            if (outBuffer_.size() >= kFlushThreshold) {
                FlushBuffer();
            }
        }

        void FlushBuffer() {
            if (!outBuffer_.empty()) {
                file_.write(outBuffer_.data(), static_cast<std::streamsize>(outBuffer_.size()));
                outBuffer_.clear();
            }
        }

        static constexpr std::size_t kFlushThreshold = 256 * 1024;

        std::ofstream& file_;
        LineEnding     ending_;
        bool           trim_;
        bool           ensureFinalNewline_;

        std::string outBuffer_;
        std::string pendingWhitespace_;
        std::size_t pendingNewlineCount_ = 0;
        bool        wroteAnything_       = false;
        bool        endsWithNewline_     = false;
    };

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

    // Undo/Redo restore a full prior storage snapshot rather than replaying
    // a single insert/delete, so there's no edit-site offset/length already
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

    // progressive-huge-file-load follow-up: this used to run against two
    // fully-materialized std::strings (Storage_->ToString()) -- for a
    // multi-GB piece-table buffer that's the exact freeze/OOM the huge-file
    // feature exists to eliminate, and it's not even huge-load-specific:
    // any Undo()/Redo() on any huge buffer paid it. CommonPrefixLength/
    // CommonSuffixLength below read through ITextStorage::Substring in
    // exponentially-growing blocks instead of ever materializing either
    // side whole -- total bytes actually read is bounded to O(the common
    // region actually found) via the standard doubling-search argument
    // (geometric series dominated by its last term), so a single localized
    // edit (or a single background load-append) costs O(edit size)
    // regardless of total document size, whether the edit sits near the
    // start, middle, or end of the buffer. The one case that's still
    // O(document size) is a genuine full-content match (both common-prefix
    // and common-suffix walks have to run to completion to prove it) --
    // unavoidable for an exact-equality question, but still bounded-memory
    // streaming via Substring rather than one giant allocation-plus-compare.
    std::size_t CommonPrefixLength(const ITextStorage& a, const ITextStorage& b) {
        const std::size_t maxLen = std::min(a.ByteLength(), b.ByteLength());
        std::size_t        checked   = 0;
        std::size_t        blockSize = 4096;
        while (checked < maxLen) {
            const std::size_t len    = std::min(blockSize, maxLen - checked);
            const std::string blockA = a.Substring(checked, len);
            const std::string blockB = b.Substring(checked, len);
            const std::size_t common =
                static_cast<std::size_t>(std::mismatch(blockA.begin(), blockA.end(), blockB.begin()).first - blockA.begin());
            checked += common;
            if (common < len) {
                return checked;
            }
            blockSize *= 2;
        }
        return maxLen;
    }

    // Mirrors CommonPrefixLength, walking backward from the end of each
    // side instead -- maxLen bounds the search (callers pass maxCommon
    // minus whatever the prefix search already claimed, so the two scans
    // can never overlap into the same bytes).
    std::size_t CommonSuffixLength(const ITextStorage& a, const ITextStorage& b, std::size_t maxLen) {
        const std::size_t aLen = a.ByteLength();
        const std::size_t bLen = b.ByteLength();
        std::size_t        checked   = 0;
        std::size_t        blockSize = 4096;
        while (checked < maxLen) {
            const std::size_t len    = std::min(blockSize, maxLen - checked);
            const std::string blockA = a.Substring(aLen - checked - len, len);
            const std::string blockB = b.Substring(bLen - checked - len, len);
            std::size_t        common = 0;
            while (common < len && blockA[len - 1 - common] == blockB[len - 1 - common]) {
                ++common;
            }
            checked += common;
            if (common < len) {
                return checked;
            }
            blockSize *= 2;
        }
        return maxLen;
    }

    // Bounded byte-for-byte equality -- length-mismatch is O(1); otherwise
    // the same doubling walk as CommonPrefixLength, so two buffers that
    // differ near the start are cheap to tell apart even at huge size.
    bool StorageContentEquals(const ITextStorage& a, const ITextStorage& b) {
        return a.ByteLength() == b.ByteLength() && CommonPrefixLength(a, b) == a.ByteLength();
    }

    std::optional<ChangedSpan> ChangedByteRange(const ITextStorage& oldStorage, const ITextStorage& newStorage) {
        const std::size_t oldLen = oldStorage.ByteLength();
        const std::size_t newLen = newStorage.ByteLength();
        const std::size_t maxCommon = std::min(oldLen, newLen);

        const std::size_t prefix = CommonPrefixLength(oldStorage, newStorage);
        if (prefix == oldLen && prefix == newLen) {
            return std::nullopt;
        }
        const std::size_t maxSuffix = maxCommon - prefix;
        const std::size_t suffix    = CommonSuffixLength(oldStorage, newStorage, maxSuffix);
        return ChangedSpan{prefix, oldLen - suffix, prefix, newLen - suffix};
    }
} // namespace

Buffer::Buffer(std::string name, Rope initialContent) : Name_(std::move(name)),
                                                        Storage_(std::make_unique<RopeStorage>(std::move(initialContent))),
                                                        UndoTree_(Storage_->Clone()),
                                                        SavedSnapshot_(Storage_->Clone()) {
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
    const bool likelyBinary = LooksBinary(path);
    if (!allowBinary && likelyBinary) {
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

    // crlf-handling follow-up: detect before normalizing -- the detected
    // ending is exactly what SaveToFile later re-expands LF back into, so
    // opening and immediately saving a CRLF file round-trips byte-for-byte
    // rather than silently converting it to LF. Storage_ itself only ever
    // holds the normalized, LF-only form (see Text/LineEnding.h).
    const LineEnding detectedEnding = DetectLineEnding(content);
    if (HasCarriageReturn(content)) {
        content = NormalizeToLf(content);
    }

    Buffer buffer(path.filename().string(), Rope(content));
    buffer.Path_         = path;
    buffer.LineEnding_   = detectedEnding;
    buffer.LikelyBinary_ = likelyBinary; // only ever true here if allowBinary let a real binary-detected open through
    if (!timestampError) {
        buffer.DiskTimestamp_ = diskTime;
    }
    return buffer;
}

Buffer Buffer::FromHugeFile(const std::filesystem::path& path, bool allowBinary) {
    // Same cheap-fail-fast reasoning as FromFile above: LooksBinary only
    // reads the first 8 KiB, so this stays cheap regardless of the file's
    // real size.
    const bool likelyBinary = LooksBinary(path);
    if (!allowBinary && likelyBinary) {
        throw BinaryFileError("ned: refusing to open binary file as text: " + path.string());
    }

    std::error_code                       timestampError;
    const std::filesystem::file_time_type diskTime = std::filesystem::last_write_time(path, timestampError);

    PieceTable table = PieceTable::FromFile(path); // throws MappedFileError on failure

    // huge-file-editing follow-up: Storage_ is always LF-only throughout
    // this class -- normalizing CRLF here would mean rewriting every line
    // in the piece table, defeating the entire point of this path (see
    // FromHugeFile's own doc comment in Buffer.h). Detected via one
    // streaming pass (ForEachChunk, never materializes the whole document)
    // -- this does mean the file's pages get faulted back in a second time
    // after PieceTable::FromFile's own scan already released them
    // (MappedFile.h's residency model), a known minor follow-up, not a
    // correctness issue.
    //
    // open-binary-anyway follow-up: skipped entirely when allowBinary is
    // set -- a confirmed binary open has no line-ending semantics to
    // protect (a 0x0D byte in binary content isn't a "line ending" at all),
    // and arbitrary binary content is essentially guaranteed to contain a
    // stray CR somewhere across a multi-GB file, so running this scan for a
    // binary open would only ever pay its full-file cost to reach a refusal
    // that makes no sense for non-text content.
    if (!allowBinary) {
        bool sawCarriageReturn = false;
        table.ForEachChunk([&](std::string_view chunk) {
            if (!sawCarriageReturn && HasCarriageReturn(chunk)) {
                sawCarriageReturn = true;
            }
        });
        if (sawCarriageReturn) {
            throw std::runtime_error("ned: huge-file opening does not yet support CRLF/CR line endings (" +
                                     path.string() + ") -- open with the normal loader instead, or convert to LF first");
        }
    }

    // A leading UTF-8 BOM is stripped the same way FromFile does above --
    // cheap even here, a fixed 3-byte prefix check plus (if present) one
    // small Erased() at offset 0, not a full-document scan.
    if (table.ByteLength() >= kUtf8Bom.size() && table.Substring(0, kUtf8Bom.size()) == kUtf8Bom) {
        table = table.Erased(0, kUtf8Bom.size());
    }

    Buffer buffer(path.filename().string());
    buffer.Storage_       = std::make_unique<PieceTableStorage>(std::move(table));
    buffer.UndoTree_      = UndoTree(buffer.Storage_->Clone());
    buffer.SavedSnapshot_ = buffer.Storage_->Clone();
    buffer.Path_          = path;
    buffer.LikelyBinary_  = likelyBinary; // only ever true here if allowBinary let a real binary-detected open through
    // Storage_ is always LF-only for this path regardless of allowBinary --
    // for a text open this is guaranteed by the refusal above; for a binary
    // open there's no line-ending convention to detect at all, so LF is
    // just this class's fixed save-time convention, not a claim about the
    // raw byte content.
    buffer.LineEnding_ = LineEnding::LF;
    if (!timestampError) {
        buffer.DiskTimestamp_ = diskTime;
    }

    // disk-space-safety follow-up: a soft, overridable downgrade -- see
    // FromHugeFile's own doc comment in Buffer.h for the full contract.
    // The file still opens either way; only editability is at stake here.
    if (HugeFileDiskSpaceCheckEnabled()) {
        const DiskSpaceCheck check = CheckFreeSpaceForSave(path, buffer.Storage_->ByteLength(), HugeFileMinFreeSpaceMultiplier());
        if (!check.sufficient) {
            buffer.SetReadOnly(true, "not enough free disk space to safely save this file (need ~" +
                                         FormatBytesHuman(check.requiredBytes) + " free, ~" +
                                         FormatBytesHuman(check.availableBytes) +
                                         " available) -- run toggle-read-only to edit anyway");
        }
    }

    return buffer;
}

Buffer Buffer::NewFile(std::filesystem::path path) {
    Buffer buffer(path.filename().string());
    buffer.Path_ = std::move(path);
    return buffer;
}

void Buffer::SaveToFile(const std::filesystem::path& path, bool ensureFinalNewline, bool trimTrailingWhitespace,
                        std::optional<LineEnding> lineEndingOverride) {
    // progressive-huge-file-load follow-up: a still-loading huge buffer is
    // now genuinely editable (ReadOnly() no longer implies "can't save" the
    // way it used to via Loading_) -- without this guard, nothing else
    // would stop a mid-load save from silently writing a truncated,
    // not-yet-fully-read version of the file over the real one. Checked
    // first, before the disk-space check below, since it's a cheap,
    // unconditional refusal rather than a size-dependent one.
    if (Loading_) {
        throw std::runtime_error("ned: cannot save \"" + Name_ + "\" -- still loading in the background");
    }

    // Write to a sibling temp file and rename over the target so a failure
    // partway through (e.g. disk full) can't leave the original truncated or
    // corrupted -- std::filesystem::rename is atomic on POSIX when both
    // paths are on the same filesystem, which a sibling file guarantees.
    const std::filesystem::path tempPath = path.string() + ".ned-tmp";
    const LineEnding            effectiveEnding = lineEndingOverride.value_or(LineEnding_);

    // disk-space-safety follow-up: the hard backstop -- no override, unlike
    // FromHugeFile's open-time downgrade (see SaveToFile's own doc comment
    // in Buffer.h). Checked before the temp file is even opened, so an
    // unsafe save never wastes I/O writing something that would fail
    // partway through anyway.
    if (Storage_->IsHuge() && HugeFileDiskSpaceCheckEnabled()) {
        const DiskSpaceCheck check = CheckFreeSpaceForSave(path, Storage_->ByteLength(), HugeFileMinFreeSpaceMultiplier());
        if (!check.sufficient) {
            throw std::runtime_error("ned: not enough free disk space to safely save \"" + path.string() + "\" (need ~" +
                                     FormatBytesHuman(check.requiredBytes) + " free, ~" + FormatBytesHuman(check.availableBytes) +
                                     " available)");
        }
    }

    {
        std::ofstream file(tempPath, std::ios::binary | std::ios::trunc);
        if (!file) {
            throw std::runtime_error("ned: cannot open file for writing: " + tempPath.string());
        }

        if (Storage_->IsHuge()) {
            // huge-file-editing follow-up: same trim/ensureFinalNewline/
            // line-ending pipeline as the non-huge path below, but streamed
            // through StreamingSaveWriter instead of materializing the
            // whole document into one string first -- the entire point of
            // this branch. Deliberately kept as a separate code path rather
            // than routing every buffer through the streaming writer: the
            // non-huge path below is unchanged, proven, and (for a buffer
            // that's merely large, not huge, e.g. tens/hundreds of MB via
            // the async-loader tier) faster than a byte-at-a-time state
            // machine would be -- no reason to pay that cost when the
            // simple whole-string approach is already correct and cheap
            // enough for anything below HugeFileThreshold.
            StreamingSaveWriter writer(file, effectiveEnding, trimTrailingWhitespace, ensureFinalNewline);
            Storage_->ForEachChunk([&writer](std::string_view chunk) { writer(chunk); });
            writer.Finish();

            if (!file) {
                file.close();
                std::filesystem::remove(tempPath);
                throw std::runtime_error("ned: error writing file: " + tempPath.string());
            }
        }
        else {
            std::string content = Storage_->ToString();
            // trim-on-save follow-up: strips trailing spaces/tabs from every
            // line, then collapses any run of trailing blank lines down to
            // nothing -- ensureFinalNewline below is what puts exactly one '\n'
            // back if the caller still wants one. Disk-only, same reasoning as
            // ensureFinalNewline itself: only this local copy is touched, never
            // Storage_ (see Editor/TrimOnSave.h).
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
            // Storage_ itself is never touched, only this local copy that's about
            // to be written; see the ensureFinalNewline doc comment on the
            // header for why that's deliberate.
            if (ensureFinalNewline && !content.empty() && content.back() != '\n') {
                content.push_back('\n');
            }

            // crlf-handling follow-up: re-expand LF back to whichever ending
            // this save should use -- lineEndingOverride lets a caller apply
            // Editor::ResolveLineEndingForSave's policy (Force*) without Text/
            // depending on Editor/; absent an override, this just keeps writing
            // whatever LineEnding_ already tracks (the common Preserve case).
            // Content up to here is always LF-only (Storage_->ToString() never
            // holds a '\r'), so this is always the last transform before write.
            if (effectiveEnding != LineEnding::LF) {
                content = ApplyLineEnding(content, effectiveEnding);
            }

            file.write(content.data(), static_cast<std::streamsize>(content.size()));

            if (!file) {
                file.close();
                std::filesystem::remove(tempPath);
                throw std::runtime_error("ned: error writing file: " + tempPath.string());
            }
        }
    } // closed here, so its contents are flushed before the rename below

    std::error_code ec;
    std::filesystem::rename(tempPath, path, ec);
    if (ec) {
        std::filesystem::remove(tempPath);
        throw std::runtime_error("ned: cannot save file: " + path.string() + " (" + ec.message() + ")");
    }

    Path_          = path;
    LineEnding_    = effectiveEnding;
    SavedSnapshot_ = Storage_->Clone();
    UnsavedChangeRanges_.clear();
    ++UnsavedChangeGeneration_;
    CaptureDiskTimestamp();
}

void Buffer::Save(bool ensureFinalNewline, bool trimTrailingWhitespace, std::optional<LineEnding> lineEndingOverride) {
    if (!Path_) {
        throw std::runtime_error("ned: buffer \"" + Name_ + "\" has no associated file path");
    }
    SaveToFile(*Path_, ensureFinalNewline, trimTrailingWhitespace, lineEndingOverride);
}

LineEnding Buffer::LineEndingKind() const {
    return LineEnding_;
}

void Buffer::SetLineEndingOverride(LineEnding ending) {
    LineEnding_ = ending;
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

const ITextStorage& Buffer::Content() const {
    return *Storage_;
}

std::string Buffer::Text() const {
    return Storage_->ToString();
}

std::size_t Buffer::Size() const {
    return Storage_->ByteLength();
}

bool Buffer::ReadOnly() const {
    return ReadOnly_;
}

void Buffer::SetReadOnly(bool readOnly, std::optional<std::string> reason) {
    ReadOnly_       = readOnly;
    ReadOnlyReason_ = std::move(reason);
}

const std::optional<std::string>& Buffer::ReadOnlyReason() const {
    return ReadOnlyReason_;
}

std::string Buffer::ReadOnlyErrorMessage() const {
    if (ReadOnlyReason_) {
        return "Buffer is read-only: " + *ReadOnlyReason_;
    }
    return "Buffer is read-only.";
}

bool Buffer::LikelyBinary() const {
    return LikelyBinary_;
}

bool Buffer::BinarySafetyOverride() const {
    return BinarySafetyOverride_;
}

void Buffer::SetBinarySafetyOverride(bool overridden) {
    BinarySafetyOverride_ = overridden;
}

bool Buffer::BinarySafeguardsActive() const {
    return LikelyBinary_ && !BinarySafetyOverride_;
}

void Buffer::SetLikelyBinary(bool likelyBinary) {
    LikelyBinary_ = likelyBinary;
}

bool Buffer::IsLoading() const {
    return Loading_;
}

void Buffer::MarkLoading(bool forceReadOnly) {
    Loading_ = true;
    if (forceReadOnly) {
        ReadOnly_ = true;
    }
}

void Buffer::SetLoadProgress(std::shared_ptr<LoadProgress> progress) {
    LoadProgress_ = std::move(progress);
}

const LoadProgress* Buffer::CurrentLoadProgress() const {
    return LoadProgress_.get();
}

void Buffer::ReplaceContentForLoad(Rope content) {
    Storage_ = std::make_unique<RopeStorage>(std::move(content));
    ++ContentGeneration_;
}

void Buffer::FinishLoad(Rope content, std::optional<LineEnding> detectedEnding) {
    ReadOnly_      = false; // undoes MarkLoading(true)'s forced read-only -- see that method's own doc comment
    Storage_       = std::make_unique<RopeStorage>(std::move(content));
    UndoTree_      = UndoTree(Storage_->Clone());
    SavedSnapshot_ = Storage_->Clone();
    if (detectedEnding) {
        LineEnding_ = *detectedEnding;
    }
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

void Buffer::ReplaceContentForHugeLoad(PieceTable content) {
    // Both start identical -- Storage_/SavedSnapshot_ must already be
    // PieceTableStorage by the time AppendHugeLoadChunk's dynamic_cast runs
    // for the second chunk-group, and SavedSnapshot_ tracking "pure disk
    // content read so far" starts here, at the very first content this
    // placeholder ever had (it was an empty RopeStorage until now).
    Storage_       = std::make_unique<PieceTableStorage>(content);
    SavedSnapshot_ = std::make_unique<PieceTableStorage>(std::move(content));
    ++ContentGeneration_;
}

void Buffer::AppendHugeLoadChunk(PieceTable fragment) {
    auto* current = dynamic_cast<PieceTableStorage*>(Storage_.get());
    auto* saved   = dynamic_cast<PieceTableStorage*>(SavedSnapshot_.get());
    // Only ever called on a buffer that came through ReplaceContentForHugeLoad
    // first -- see Buffer.h's own doc comment on the call sequence.
    assert(current != nullptr && saved != nullptr);
    Storage_       = std::make_unique<PieceTableStorage>(current->Value().Concatenated(fragment));
    SavedSnapshot_ = std::make_unique<PieceTableStorage>(saved->Value().Concatenated(fragment));
    RecordOrAmendLoadAppend();
    ++ContentGeneration_;
}

void Buffer::FinishHugeLoad() {
    ReadOnly_      = false; // undoes MarkLoading(false)'s no-op here, but mirrors FinishLoad's own unconditional reset
    LineEnding_    = LineEnding::LF; // always true for this path -- see FromHugeFile's own doc comment
    Loading_       = false;
    LoadProgress_.reset();
    ++ContentGeneration_;
    CaptureDiskTimestamp();

    // disk-space-safety follow-up: same soft, overridable downgrade
    // FromHugeFile's own tail applies -- see that function's doc comment in
    // Buffer.h for the full contract. The file still opens either way; only
    // editability is at stake here.
    if (Path_ && HugeFileDiskSpaceCheckEnabled()) {
        const DiskSpaceCheck check = CheckFreeSpaceForSave(*Path_, Storage_->ByteLength(), HugeFileMinFreeSpaceMultiplier());
        if (!check.sufficient) {
            SetReadOnly(true, "not enough free disk space to safely save this file (need ~" +
                                   FormatBytesHuman(check.requiredBytes) + " free, ~" +
                                   FormatBytesHuman(check.availableBytes) +
                                   " available) -- run toggle-read-only to edit anyway");
        }
    }
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

    Storage_ = std::move(fresh.Storage_);
    Point_   = SnapToGraphemeBoundary(*Storage_, std::min(Point_, Storage_->ByteLength()));
    Mark_.reset();
    SecondaryCursors_.clear();
    AddedCursorOrder_.clear();
    NarrowedRange_.reset();
    FoldMarkers_.clear();
    ++FoldGeneration_;

    RecordOrAmendUndo(/*canAmend=*/false); // one normal, undoable step
    GoalColumn_.reset();
    ++ContentGeneration_;

    // The buffer now matches disk by definition.
    SavedSnapshot_ = Storage_->Clone();
    UnsavedChangeRanges_.clear();
    ++UnsavedChangeGeneration_;
    DiskTimestamp_ = fresh.DiskTimestamp_;
}

std::size_t Buffer::MergeExternalChanges() {
    if (!Path_) {
        throw std::runtime_error("ned: buffer \"" + Name_ + "\" has no associated file path");
    }
    Buffer fresh = FromFile(*Path_); // throws on any read failure, leaving this buffer untouched

    const std::string       base   = SavedSnapshot_->ToString();
    const std::string       ours   = Storage_->ToString();
    const std::string       theirs = fresh.Storage_->ToString();
    const text::MergeResult result = text::ThreeWayMerge(base, ours, theirs);

    Storage_ = std::make_unique<RopeStorage>(Rope(result.mergedText));
    Point_ = SnapToGraphemeBoundary(*Storage_, std::min(result.firstConflictOffset.value_or(Point_), Storage_->ByteLength()));
    Mark_.reset();
    SecondaryCursors_.clear();
    AddedCursorOrder_.clear();
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
    if (Storage_->ByteLength() > 0) {
        MergeUnsavedRange(UnsavedChangeRanges_, 0, Storage_->ByteLength());
    }
    ++UnsavedChangeGeneration_;

    // The new "last synced with disk" baseline is the freshly read disk
    // content, NOT the merged result -- see this method's own doc comment
    // in Buffer.h for why.
    SavedSnapshot_ = std::move(fresh.Storage_);
    DiskTimestamp_ = fresh.DiskTimestamp_;

    return result.conflictCount;
}

bool Buffer::HasConflictMarkers() const {
    static constexpr std::string_view kMarker = "<<<<<<< ";

    const std::size_t total = Storage_->ByteLength();
    if (total == 0) {
        return false;
    }

    // huge-file-search-and-save follow-up: scans in bounded windows via
    // Substring rather than text::HasConflictMarkers(Storage_->ToString()),
    // which would materialize the whole buffer just to answer a boolean.
    // Each window reads one extra byte at its front (when not at the very
    // start of the document) so `window[pos - 1]` always sees the real
    // preceding byte, and one marker's worth extra at its back so a marker
    // starting on the last in-range byte still has room to be read whole --
    // together that makes every window's line-start/marker check exactly
    // equivalent to scanning the un-windowed text, just without ever
    // holding more than kWindow bytes at a time.
    constexpr std::size_t kWindow = 4 * 1024 * 1024;
    std::size_t           offset  = 0;
    while (offset < total) {
        const std::size_t lookback  = offset == 0 ? 0 : 1;
        const std::size_t start     = offset - lookback;
        const std::size_t coreLen   = std::min(kWindow, total - offset);
        const std::size_t windowLen = std::min(lookback + coreLen + (kMarker.size() - 1), total - start);
        const std::string window    = Storage_->Substring(start, windowLen);

        const std::size_t scanFrom = lookback;
        const std::size_t scanTo   = lookback + coreLen;
        for (std::size_t pos = scanFrom; pos < scanTo; ++pos) {
            const bool atLineStart = (start + pos == 0) || window[pos - 1] == '\n';
            if (atLineStart && window.compare(pos, kMarker.size(), kMarker) == 0) {
                return true;
            }
        }
        offset += kWindow;
    }
    return false;
}

void Buffer::RestoreContent(std::string_view content) {
    Storage_ = std::make_unique<RopeStorage>(Rope(content));
    Point_ = SnapToGraphemeBoundary(*Storage_, std::min(Point_, Storage_->ByteLength()));
    Mark_.reset();
    SecondaryCursors_.clear();
    AddedCursorOrder_.clear();
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
    if (Storage_->ByteLength() > 0) {
        MergeUnsavedRange(UnsavedChangeRanges_, 0, Storage_->ByteLength());
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
    return Storage_->ByteLength() == 0 && SavedSnapshot_->ByteLength() != 0;
}

std::size_t Buffer::ContentGeneration() const {
    return ContentGeneration_;
}

std::size_t Buffer::Point() const {
    return Point_;
}

void Buffer::SetPoint(std::size_t byteOffset) {
    Point_    = SnapToGraphemeBoundary(*Storage_, byteOffset);
    CanAmend_ = false;
    GoalColumn_.reset();
}

void Buffer::SetMark(std::size_t byteOffset) {
    Mark_ = SnapToGraphemeBoundary(*Storage_, byteOffset);
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
    cursor.point = SnapToGraphemeBoundary(*Storage_, std::min(point, Storage_->ByteLength()));
    if (mark) {
        cursor.mark = SnapToGraphemeBoundary(*Storage_, std::min(*mark, Storage_->ByteLength()));
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
    AddedCursorOrder_.push_back(cursor.point);
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
    AddedCursorOrder_.clear();
}

bool Buffer::RemoveLastAddedCursor() {
    while (!AddedCursorOrder_.empty()) {
        const std::size_t target = AddedCursorOrder_.back();
        AddedCursorOrder_.pop_back();
        const std::size_t before = SecondaryCursors_.size();
        std::erase_if(SecondaryCursors_, [target](const Cursor& cursor) { return cursor.point == target; });
        if (SecondaryCursors_.size() != before) {
            return true;
        }
        // Stale entry (already removed, or NormalizeSecondaryCursors merged
        // it away) -- keep walking back through older additions.
    }
    return false;
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
        UndoTree_.Record(Storage_->Clone());
        CanAmend_       = false;
        UndoGroupDirty_ = false;
    }
}

void Buffer::RecordOrAmendUndo(bool canAmend) {
    CanAmendLoadAppend_ = false; // a real edit must never let a later load-append silently amend into it
    if (UndoGroupDepth_ > 0) {
        UndoGroupDirty_ = true;
        return;
    }
    if (canAmend && CanAmend_) {
        UndoTree_.Amend(Storage_->Clone());
        return;
    }
    UndoTree_.Record(Storage_->Clone());
    CanAmend_ = canAmend;
}

void Buffer::RecordOrAmendLoadAppend() {
    CanAmend_ = false; // a load-append must never let a later keystroke silently amend into it
    if (UndoGroupDepth_ > 0) {
        UndoGroupDirty_ = true;
        return;
    }
    if (CanAmendLoadAppend_) {
        UndoTree_.Amend(Storage_->Clone());
        return;
    }
    UndoTree_.Record(Storage_->Clone());
    CanAmendLoadAppend_ = true;
}

void Buffer::RelocateSecondaryCursorsForInsert(std::size_t insertOffset, std::size_t length) {
    for (Cursor& cursor : SecondaryCursors_) {
        cursor.point = RelocateForInsert(cursor.point, insertOffset, length);
        if (cursor.mark) {
            *cursor.mark = RelocateForInsert(*cursor.mark, insertOffset, length);
        }
    }
    for (std::size_t& point : AddedCursorOrder_) {
        point = RelocateForInsert(point, insertOffset, length);
    }
}

void Buffer::RelocateSecondaryCursorsForDelete(std::size_t rangeStart, std::size_t rangeEnd) {
    for (Cursor& cursor : SecondaryCursors_) {
        cursor.point = RelocateForDelete(cursor.point, rangeStart, rangeEnd);
        if (cursor.mark) {
            *cursor.mark = RelocateForDelete(*cursor.mark, rangeStart, rangeEnd);
        }
    }
    for (std::size_t& point : AddedCursorOrder_) {
        point = RelocateForDelete(point, rangeStart, rangeEnd);
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
    start = std::min(start, Storage_->ByteLength());
    end   = std::min(end, Storage_->ByteLength());

    const std::size_t totalLines   = Storage_->LineCount();
    const std::size_t startLine    = Storage_->ByteOffsetToLine(start);
    const std::size_t snappedStart = Storage_->LineToByteOffset(startLine);

    const std::size_t endLine = Storage_->ByteOffsetToLine(end);
    const std::size_t snappedEnd =
        (endLine + 1 < totalLines) ? Storage_->LineToByteOffset(endLine + 1) : Storage_->ByteLength();

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
    std::size_t markEnd   = std::min(rangeStart + 1, Storage_->ByteLength());
    if (markEnd <= markStart && markStart > 0) {
        markStart = markStart - 1;
        markEnd   = markStart + 1;
    }
    if (markEnd > markStart) {
        MergeUnsavedRange(UnsavedChangeRanges_, markStart, markEnd);
    }
    ++UnsavedChangeGeneration_;
}

void Buffer::SetSnippetRanges(std::vector<SnippetRange> ranges) {
    SnippetRanges_ = std::move(ranges);
}

const std::vector<Buffer::SnippetRange>& Buffer::SnippetRanges() const {
    return SnippetRanges_;
}

void Buffer::ClearSnippetRanges() {
    SnippetRanges_.clear();
}

void Buffer::SetActiveSnippetRange(std::size_t id) {
    for (SnippetRange& range : SnippetRanges_) {
        range.active = range.id == id;
    }
}

void Buffer::UpdateSnippetRange(std::size_t id, std::size_t start, std::size_t end) {
    for (SnippetRange& range : SnippetRanges_) {
        if (range.id == id) {
            range.start = start;
            range.end   = end;
            return;
        }
    }
}

void Buffer::RelocateSnippetRangesForInsert(std::size_t insertOffset, std::size_t length) {
    for (SnippetRange& range : SnippetRanges_) {
        if (range.active) {
            // The active field grows when text lands at either of its own
            // edges: an insert exactly at start stays outside-left of the
            // shift (start keeps its position, the new text is inside), an
            // insert exactly at end pushes end right.
            if (insertOffset < range.start) {
                range.start += length;
            }
            if (insertOffset <= range.end) {
                range.end += length;
            }
        }
        else {
            // An inactive field excludes a boundary insert entirely, so an
            // insert at the seam between two adjacent fields extends only
            // whichever one is active.
            if (insertOffset <= range.start) {
                range.start += length;
            }
            if (insertOffset < range.end) {
                range.end += length;
            }
        }
        // A degenerate inactive range sitting exactly at insertOffset gets
        // its start pushed but not its end -- re-order rather than leaving
        // an inverted range behind.
        range.end = std::max(range.start, range.end);
    }
}

void Buffer::RelocateSnippetRangesForDelete(std::size_t rangeStart, std::size_t rangeEnd) {
    for (SnippetRange& range : SnippetRanges_) {
        range.start = RelocateForDelete(range.start, rangeStart, rangeEnd);
        range.end   = RelocateForDelete(range.end, rangeStart, rangeEnd);
        // A field fully inside the deletion becomes degenerate and is kept
        // -- an emptied field is still a navigable, refillable field.
    }
}

void Buffer::SetExcerptRanges(std::vector<ExcerptRange> ranges) {
    ExcerptRanges_ = std::move(ranges);
}

const std::vector<Buffer::ExcerptRange>& Buffer::ExcerptRanges() const {
    return ExcerptRanges_;
}

void Buffer::ClearExcerptRanges() {
    ExcerptRanges_.clear();
}

void Buffer::MarkExcerptRangeCommitted(std::size_t start, std::size_t end, std::string newOriginalText,
                                       std::size_t newSourceStart, std::size_t newSourceEnd) {
    for (ExcerptRange& range : ExcerptRanges_) {
        if (range.start == start && range.end == end) {
            range.originalText   = std::move(newOriginalText);
            range.sourceStartByte = newSourceStart;
            range.sourceEndByte   = newSourceEnd;
            return;
        }
    }
}

void Buffer::RelocateExcerptRangesForInsert(std::size_t insertOffset, std::size_t length) {
    for (ExcerptRange& range : ExcerptRanges_) {
        // Every range grows when text lands at either of its own edges --
        // no active/inactive distinction (see the header's own doc
        // comment): excerpts are always separated by protected chrome, so
        // there's no adjacent-field seam to disambiguate the way
        // SnippetRange's gravity needs to.
        if (insertOffset < range.start) {
            range.start += length;
        }
        if (insertOffset <= range.end) {
            range.end += length;
        }
        range.end = std::max(range.start, range.end);
    }
}

void Buffer::RelocateExcerptRangesForDelete(std::size_t rangeStart, std::size_t rangeEnd) {
    for (ExcerptRange& range : ExcerptRanges_) {
        range.start = RelocateForDelete(range.start, rangeStart, rangeEnd);
        range.end   = RelocateForDelete(range.end, rangeStart, rangeEnd);
        // A range fully inside the deletion becomes degenerate and is kept
        // -- see ExcerptRange's own doc comment on why that's meaningful
        // here (a real deletion to commit), not just tolerated.
    }
}

bool Buffer::CanInsertAtExcerpt(std::size_t offset) const {
    if (ExcerptRanges_.empty()) {
        return true;
    }
    for (const ExcerptRange& range : ExcerptRanges_) {
        if (range.editable && offset >= range.start && offset <= range.end) {
            return true;
        }
    }
    return false;
}

bool Buffer::CanDeleteExcerptRange(std::size_t rangeStart, std::size_t rangeEnd) const {
    if (ExcerptRanges_.empty()) {
        return true;
    }
    for (const ExcerptRange& range : ExcerptRanges_) {
        if (range.editable && rangeStart >= range.start && rangeEnd <= range.end) {
            return true;
        }
    }
    return false;
}

void Buffer::InsertAtPoint(std::string_view text) {
    if (ReadOnly()) {
        throw std::runtime_error(ReadOnlyErrorMessage());
    }
    if (text.empty()) {
        return;
    }
    // Editable-multibuffer follow-up: a silent no-op, not a thrown error --
    // see CanInsertAtExcerpt's own doc comment for why (multi-cursor edits
    // have no per-cursor failure protocol today; a typing attempt inside an
    // excerpt's protected header/rule chrome just produces nothing, the
    // same posture ReadOnly() gives the whole-buffer case one level up).
    if (!CanInsertAtExcerpt(Point_)) {
        return;
    }

    const std::size_t insertOffset = Point_;
    Storage_                       = Storage_->Inserted(insertOffset, text);
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
    RelocateSnippetRangesForInsert(insertOffset, text.size());
    RelocateExcerptRangesForInsert(insertOffset, text.size());
    MarkUnsavedRangeInserted(insertOffset, text.size());

    RecordOrAmendUndo(/*canAmend=*/true);
    GoalColumn_.reset();
    ++ContentGeneration_;
}

void Buffer::DeleteBackwardAtPoint() {
    if (ReadOnly()) {
        throw std::runtime_error(ReadOnlyErrorMessage());
    }
    if (Point_ == 0) {
        return;
    }

    const std::size_t start = PreviousGraphemeBoundary(*Storage_, Point_);
    const std::size_t end   = Point_;
    if (!CanDeleteExcerptRange(start, end)) { // see InsertAtPoint's own comment on this posture
        return;
    }
    Storage_ = Storage_->Erased(start, end - start);

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
    RelocateSnippetRangesForDelete(start, end);
    RelocateExcerptRangesForDelete(start, end);
    MarkUnsavedRangeDeleted(start, end);

    RecordOrAmendUndo(/*canAmend=*/false);
    GoalColumn_.reset();
    ++ContentGeneration_;
}

void Buffer::DeleteForwardAtPoint() {
    if (ReadOnly()) {
        throw std::runtime_error(ReadOnlyErrorMessage());
    }
    if (Point_ >= Storage_->ByteLength()) {
        return;
    }

    const std::size_t start = Point_;
    const std::size_t end   = NextGraphemeBoundary(*Storage_, Point_);
    if (!CanDeleteExcerptRange(start, end)) { // see InsertAtPoint's own comment on this posture
        return;
    }
    Storage_ = Storage_->Erased(start, end - start);

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
    RelocateSnippetRangesForDelete(start, end);
    RelocateExcerptRangesForDelete(start, end);
    MarkUnsavedRangeDeleted(start, end);

    RecordOrAmendUndo(/*canAmend=*/false);
    GoalColumn_.reset();
    ++ContentGeneration_;
}

std::string Buffer::DeleteRange(std::size_t byteOffset, std::size_t byteLength) {
    if (ReadOnly()) {
        throw std::runtime_error(ReadOnlyErrorMessage());
    }
    byteOffset = std::min(byteOffset, Storage_->ByteLength());
    byteLength = std::min(byteLength, Storage_->ByteLength() - byteOffset);

    if (byteLength == 0) {
        return {};
    }

    const std::size_t rangeEnd = byteOffset + byteLength;
    if (!CanDeleteExcerptRange(byteOffset, rangeEnd)) { // see InsertAtPoint's own comment on this posture
        return {};
    }
    std::string deleted = Storage_->Substring(byteOffset, byteLength);
    Storage_             = Storage_->Erased(byteOffset, byteLength);

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
    RelocateSnippetRangesForDelete(byteOffset, rangeEnd);
    RelocateExcerptRangesForDelete(byteOffset, rangeEnd);
    MarkUnsavedRangeDeleted(byteOffset, rangeEnd);

    RecordOrAmendUndo(/*canAmend=*/false);
    GoalColumn_.reset();
    ++ContentGeneration_;
    return deleted;
}

void Buffer::InsertAt(std::size_t byteOffset, std::string_view text) {
    if (ReadOnly()) {
        throw std::runtime_error(ReadOnlyErrorMessage());
    }
    InsertAtImpl(byteOffset, text);
}

void Buffer::AppendWhileReadOnly(std::string_view text) {
    if (!ReadOnly_) {
        throw std::logic_error("AppendWhileReadOnly called on a writable buffer.");
    }
    InsertAtImpl(Storage_->ByteLength(), text);
}

void Buffer::InsertAtImpl(std::size_t byteOffset, std::string_view text) {
    byteOffset = std::min(byteOffset, Storage_->ByteLength());

    if (text.empty()) {
        return;
    }
    if (!CanInsertAtExcerpt(byteOffset)) { // see InsertAtPoint's own comment on this posture
        return;
    }

    Storage_ = Storage_->Inserted(byteOffset, text);

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
    RelocateSnippetRangesForInsert(byteOffset, text.size());
    RelocateExcerptRangesForInsert(byteOffset, text.size());
    MarkUnsavedRangeInserted(byteOffset, text.size());

    RecordOrAmendUndo(/*canAmend=*/false);
    GoalColumn_.reset();
    ++ContentGeneration_;
}

void Buffer::MoveForward() {
    Point_    = NextGraphemeBoundary(*Storage_, Point_);
    CanAmend_ = false;
    GoalColumn_.reset();
}

void Buffer::MoveBackward() {
    Point_    = PreviousGraphemeBoundary(*Storage_, Point_);
    CanAmend_ = false;
    GoalColumn_.reset();
}

void Buffer::MoveForwardWord() {
    const std::size_t total  = Storage_->ByteLength();
    std::size_t       offset = Point_;

    while (offset < total && !IsWordCodepoint(Storage_->CodepointAt(offset).codepoint)) {
        offset = Storage_->NextCodepointBoundary(offset);
    }
    while (offset < total && IsWordCodepoint(Storage_->CodepointAt(offset).codepoint)) {
        offset = Storage_->NextCodepointBoundary(offset);
    }

    Point_    = SnapToGraphemeBoundary(*Storage_, offset);
    CanAmend_ = false;
    GoalColumn_.reset();
}

void Buffer::MoveBackwardWord() {
    std::size_t offset = Point_;

    while (offset > 0) {
        const std::size_t previous = Storage_->PreviousCodepointBoundary(offset);
        if (IsWordCodepoint(Storage_->CodepointAt(previous).codepoint)) {
            break;
        }
        offset = previous;
    }
    while (offset > 0) {
        const std::size_t previous = Storage_->PreviousCodepointBoundary(offset);
        if (!IsWordCodepoint(Storage_->CodepointAt(previous).codepoint)) {
            break;
        }
        offset = previous;
    }

    Point_    = SnapToGraphemeBoundary(*Storage_, offset);
    CanAmend_ = false;
    GoalColumn_.reset();
}

void Buffer::MoveForwardSentence() {
    const std::size_t total  = Storage_->ByteLength();
    std::size_t       offset = Point_;

    while (offset < total && !IsSentenceEndCodepoint(Storage_->CodepointAt(offset).codepoint)) {
        offset = Storage_->NextCodepointBoundary(offset);
    }
    if (offset < total) {
        offset = Storage_->NextCodepointBoundary(offset); // past the sentence-ending punctuation itself
        while (offset < total && IsSpaceOrNewlineCodepoint(Storage_->CodepointAt(offset).codepoint)) {
            offset = Storage_->NextCodepointBoundary(offset);
        }
    }

    Point_    = SnapToGraphemeBoundary(*Storage_, offset);
    CanAmend_ = false;
    GoalColumn_.reset();
}

void Buffer::MoveBackwardSentence() {
    std::size_t offset = Point_;

    // Skip back over whitespace directly before point -- mirrors forward's
    // own post-punctuation whitespace skip, so this can land right back
    // where a preceding MoveForwardSentence call would have stopped.
    while (offset > 0) {
        const std::size_t previous = Storage_->PreviousCodepointBoundary(offset);
        if (!IsSpaceOrNewlineCodepoint(Storage_->CodepointAt(previous).codepoint)) {
            break;
        }
        offset = previous;
    }
    // Sitting right after a sentence-ending mark (the common case just
    // after the whitespace skip above): hop back over it too, or the scan
    // below would immediately re-find that same mark and refuse to move.
    if (offset > 0) {
        const std::size_t previous = Storage_->PreviousCodepointBoundary(offset);
        if (IsSentenceEndCodepoint(Storage_->CodepointAt(previous).codepoint)) {
            offset = previous;
        }
    }
    while (offset > 0) {
        const std::size_t previous = Storage_->PreviousCodepointBoundary(offset);
        if (IsSentenceEndCodepoint(Storage_->CodepointAt(previous).codepoint)) {
            break;
        }
        offset = previous;
    }
    // offset now sits right after the previous sentence's own end mark (or
    // 0) -- skip forward over whitespace to land on the first real
    // character, matching MoveForwardSentence's own landing spot.
    while (offset < Storage_->ByteLength() && IsSpaceOrNewlineCodepoint(Storage_->CodepointAt(offset).codepoint)) {
        offset = Storage_->NextCodepointBoundary(offset);
    }

    Point_    = SnapToGraphemeBoundary(*Storage_, offset);
    CanAmend_ = false;
    GoalColumn_.reset();
}

std::size_t Buffer::ByteOffsetForLineAndColumn(std::size_t line, std::size_t column, std::size_t tabWidth) const {
    const std::size_t totalLines = Storage_->LineCount();
    line                         = std::min(line, totalLines - 1);

    const std::size_t lineStart = Storage_->LineToByteOffset(line);
    const std::size_t lineEnd   = (line + 1 < totalLines) ? Storage_->LineToByteOffset(line + 1) - 1 : Storage_->ByteLength();

    if (tabWidth <= 1) {
        const std::size_t lineStartCodepoint = Storage_->ByteOffsetToCodepointOffset(lineStart);
        const std::size_t lineLength         = Storage_->ByteOffsetToCodepointOffset(lineEnd) - lineStartCodepoint;

        const std::size_t landingCodepoint = lineStartCodepoint + std::min(column, lineLength);
        return Storage_->CodepointOffsetToByteOffset(landingCodepoint);
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
            const std::size_t lineEndCodepoint = Storage_->ByteOffsetToCodepointOffset(lineEnd);
            const std::size_t landingCodepoint = std::min(Storage_->ByteOffsetToCodepointOffset(offset) + remainingColumns,
                                                          lineEndCodepoint);
            return Storage_->CodepointOffsetToByteOffset(landingCodepoint);
        }
        const auto decoded = Storage_->CodepointAt(offset);
        visualColumn += (decoded.codepoint == U'\t') ? tabWidth : 1;
        offset += decoded.byteLength;
        ++steps;
    }
    return offset;
}

std::size_t Buffer::VisualColumnForByteOffset(std::size_t lineStart, std::size_t byteOffset,
                                              std::size_t tabWidth) const {
    if (tabWidth <= 1) {
        return Storage_->ByteOffsetToCodepointOffset(byteOffset) - Storage_->ByteOffsetToCodepointOffset(lineStart);
    }

    std::size_t offset = lineStart;
    std::size_t column = 0;
    std::size_t steps  = 0;
    while (offset < byteOffset) {
        if (steps >= kMaxTabAwareColumnScan) {
            // Bail out to a plain codepoint-distance approximation for the
            // remainder -- see kMaxTabAwareColumnScan's own comment.
            return column + (Storage_->ByteOffsetToCodepointOffset(byteOffset) - Storage_->ByteOffsetToCodepointOffset(offset));
        }
        const auto decoded = Storage_->CodepointAt(offset);
        column += (decoded.codepoint == U'\t') ? tabWidth : 1;
        offset += decoded.byteLength;
        ++steps;
    }
    return column;
}

void Buffer::MoveToLine(std::size_t targetLine, std::size_t tabWidth) {
    const std::size_t currentLineStart = Storage_->LineToByteOffset(Storage_->ByteOffsetToLine(Point_));
    const std::size_t desiredColumn    = GoalColumn_.value_or(VisualColumnForByteOffset(currentLineStart, Point_, tabWidth));

    const std::size_t landingByte = ByteOffsetForLineAndColumn(targetLine, desiredColumn, tabWidth);

    Point_      = SnapToGraphemeBoundary(*Storage_, landingByte);
    GoalColumn_ = desiredColumn; // the un-clamped goal, not necessarily where we landed
    CanAmend_   = false;
}

void Buffer::MoveDownLines(std::size_t count, std::size_t tabWidth) {
    const std::size_t currentLine = Storage_->ByteOffsetToLine(Point_);
    const std::size_t lastLine    = Storage_->LineCount() - 1;
    if (currentLine == lastLine) {
        return; // already on the last line -- true no-op, regardless of any stale goal column
    }
    // Clamped rather than a plain currentLine + count: a page-down whose
    // count overshoots the end of a short buffer should still land on the
    // last line instead of doing nothing.
    MoveToLine(std::min(currentLine + count, lastLine), tabWidth);
}

void Buffer::MoveUpLines(std::size_t count, std::size_t tabWidth) {
    const std::size_t currentLine = Storage_->ByteOffsetToLine(Point_);
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
    Point_ = SnapToGraphemeBoundary(*Storage_, std::min(Point_, Storage_->ByteLength()));
    if (Mark_) {
        Mark_ = SnapToGraphemeBoundary(*Storage_, std::min(*Mark_, Storage_->ByteLength()));
    }
    if (NarrowedRange_) {
        // Undo/redo can swap in content of a completely different length --
        // clamp the range into whatever's now valid, and auto-widen if it
        // would otherwise become degenerate (start no longer strictly
        // before end), the same rule DeleteRange's own narrowing-tracking
        // uses.
        auto& [narrowStart, narrowEnd] = *NarrowedRange_;
        narrowStart                    = std::min(narrowStart, Storage_->ByteLength());
        narrowEnd                      = std::min(narrowEnd, Storage_->ByteLength());
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
        const std::size_t length = Storage_->ByteLength();
        std::erase_if(FoldMarkers_, [length](const auto& entry) { return entry.first > length; });
    }
    // Snippet ranges are all-or-nothing: a set with any endpoint past the
    // new end is no longer a coherent field layout, and there's no sensible
    // per-range clamp (a clamped field would overlap arbitrarily with
    // whatever's left) -- drop the whole set and let the owning session
    // treat that as its end-of-session cue. Belt-and-suspenders: the only
    // real path here (Undo/Redo) already cleared them outright.
    if (!SnippetRanges_.empty()) {
        const std::size_t length     = Storage_->ByteLength();
        const bool        anyPastEnd = std::any_of(SnippetRanges_.begin(), SnippetRanges_.end(),
                                                   [length](const SnippetRange& range) { return range.end > length; });
        if (anyPastEnd) {
            SnippetRanges_.clear();
        }
    }
}

void Buffer::Undo() {
    if (!UndoTree_.CanUndo()) {
        return;
    }
    const std::unique_ptr<ITextStorage> oldStorage = Storage_->Clone(); // O(1) -- never materializes, see ChangedByteRange's own comment
    UndoTree_.Undo();
    Storage_ = UndoTree_.Current().Clone();
    ClearSecondaryCursors(); // v1 decision -- see AddCursorAt's doc comment
    ClearSnippetRanges();    // same v1 decision -- see SnippetRange's doc comment
    ClampCursorsToContent();
    CanAmend_           = false;
    CanAmendLoadAppend_ = false;
    GoalColumn_.reset();
    ++ContentGeneration_;
    UpdateUnsavedRangesForRestore(*oldStorage);
    UpdateExcerptRangesForRestore(*oldStorage); // NOT cleared -- see ExcerptRange's own doc comment
}

void Buffer::Redo() {
    if (!UndoTree_.CanRedo()) {
        return;
    }
    const std::unique_ptr<ITextStorage> oldStorage = Storage_->Clone(); // O(1) -- never materializes, see ChangedByteRange's own comment
    UndoTree_.Redo();
    Storage_ = UndoTree_.Current().Clone();
    ClearSecondaryCursors(); // v1 decision -- see AddCursorAt's doc comment
    ClearSnippetRanges();    // same v1 decision -- see SnippetRange's doc comment
    ClampCursorsToContent();
    CanAmend_           = false;
    CanAmendLoadAppend_ = false;
    GoalColumn_.reset();
    ++ContentGeneration_;
    UpdateUnsavedRangesForRestore(*oldStorage);
    UpdateExcerptRangesForRestore(*oldStorage); // NOT cleared -- see ExcerptRange's own doc comment
}

std::vector<UndoTree::SerializedNode> Buffer::SerializeUndo() const {
    return UndoTree_.Serialize();
}

std::size_t Buffer::CurrentUndoNodeId() const {
    return UndoTree_.CurrentNodeId();
}

void Buffer::RestoreUndoTree(std::vector<UndoTree::SerializedNode> nodes, std::size_t currentId) {
    UndoTree restored = UndoTree::Deserialize(nodes, currentId);
    if (!StorageContentEquals(restored.Current(), *Storage_)) {
        throw std::runtime_error("Buffer::RestoreUndoTree: restored tree's current content doesn't match buffer content");
    }
    UndoTree_ = std::move(restored);
    CanAmend_ = false;
}

void Buffer::UpdateUnsavedRangesForRestore(const ITextStorage& oldStorage) {
    // Landed back on exactly the last-saved content, regardless of the
    // path taken to get there (this Undo()/Redo() step, or several
    // compounded with earlier ones) -- no unsaved change at all, full
    // stop, not just "nothing changed in this specific step." Found to be
    // necessary via live testing: the diff-against-oldStorage path below is
    // exact for *this* step, but an edit undone anywhere except the very
    // end of the buffer still left a real, technically-accurate 1-byte
    // marker at the edit site even once content matched disk again --
    // correct in isolation, misleading in practice (still showed as an
    // unsaved change after undoing the exact edit that caused it).
    if (StorageContentEquals(*Storage_, *SavedSnapshot_)) {
        if (!UnsavedChangeRanges_.empty()) {
            UnsavedChangeRanges_.clear();
            ++UnsavedChangeGeneration_;
        }
        return;
    }

    // Undoing/redoing restores a full prior storage snapshot rather than
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
    if (const auto span = ChangedByteRange(oldStorage, *Storage_)) {
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

void Buffer::UpdateExcerptRangesForRestore(const ITextStorage& oldStorage) {
    // Fast path for the overwhelming majority of buffers that never carry
    // excerpt ranges at all -- skips even the bounded diff below.
    if (ExcerptRanges_.empty()) {
        return;
    }
    // Same delete-half-then-insert-half composition UpdateUnsavedRangesForRestore
    // uses, over the same ChangedByteRange diff, onto
    // RelocateExcerptRangesForDelete/Insert instead of
    // MarkUnsavedRangeDeleted/Inserted -- see this method's own doc comment
    // in Buffer.h for why relocating (not clearing) is the right call here.
    if (const auto span = ChangedByteRange(oldStorage, *Storage_)) {
        if (span->oldEnd > span->oldStart) {
            RelocateExcerptRangesForDelete(span->oldStart, span->oldEnd);
        }
        if (span->newEnd > span->newStart) {
            RelocateExcerptRangesForInsert(span->newStart, span->newEnd - span->newStart);
        }
    }
}

} // namespace ned::text
