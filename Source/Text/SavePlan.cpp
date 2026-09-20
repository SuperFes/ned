#include "SavePlan.h"

#include <fstream>
#include <stdexcept>
#include <system_error>

#include "WhitespaceHygiene.h"

namespace ned::text {

namespace {
    // huge-file-editing follow-up (streaming save): reproduces the non-huge
    // save path's trim-trailing-whitespace / ensure-final-newline /
    // line-ending-expansion pipeline as a single forward streaming pass over
    // content fed in arbitrary-sized chunks (ITextStorage::ForEachChunk's
    // contract -- a chunk boundary carries no semantic meaning, a line or a
    // run of trailing whitespace can span one), instead of materializing the
    // whole document into one string first the way the non-huge path below
    // still does (deliberately left alone -- see that path's own comment on
    // why). Byte-identical output to that whole-string algorithm -- verified
    // directly, see BufferSaveEquivalenceTest.cpp, not just reasoned about.
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
        StreamingSaveWriter(std::ofstream& file, LineEnding ending, bool trim, bool ensureFinalNewline) : file_(file), ending_(ending), trim_(trim), ensureFinalNewline_(ensureFinalNewline) {
        }

        void operator()(std::string_view chunk) {
            for (char c : chunk) {
                Feed(c);
            }
        }

        // Call once after every chunk has been fed. Applies
        // ensureFinalNewline and flushes any buffered output -- does NOT
        // check the stream for a write failure itself; the caller checks
        // `file` once after this returns, same as the non-huge path
        // already does after its own single write.
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
                }
                else {
                    WriteByte(c);
                }
                return;
            }

            if (c == ' ' || c == '\t') {
                pendingWhitespace_.push_back(c);
            }
            else if (c == '\n') {
                pendingWhitespace_.clear(); // trailing on this line -- discard
                ++pendingNewlineCount_;
            }
            else {
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
            }
            else if (ending_ == LineEnding::CR) {
                outBuffer_.push_back('\r');
            }
            else {
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

    // The full content transform (trim -> ensureFinalNewline -> line-ending
    // re-expansion) plus the write itself, shared by ExecuteSavePlan's two
    // write modes -- the sibling-temp-then-rename atomic path and the in-place
    // truncate a hard-linked file needs. Deliberately leaves the stream's
    // failure state for the caller to check: the two modes recover from a
    // failed write very differently (remove the temp file vs. report a
    // possibly-truncated real file).
    void WriteBufferContent(std::ofstream& file, const ITextStorage& storage, LineEnding effectiveEnding, bool trimTrailingWhitespace,
                            bool ensureFinalNewline) {
        if (storage.IsHuge()) {
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
            storage.ForEachChunk([&writer](std::string_view chunk) { writer(chunk); });
            writer.Finish();
            return;
        }

        std::string content = storage.ToString();
        // trim-on-save follow-up (configurable-formatter follow-up: the
        // actual algorithm moved to Text/WhitespaceHygiene.h, shared with
        // Editor::ApplyHygienePass -- see that header's own doc comment for
        // why the HUGE-file streaming path just above stays a separate
        // reimplementation rather than a caller of this). Strips trailing
        // spaces/tabs from every line, then collapses any run of trailing
        // blank lines down to nothing -- ensureFinalNewline below is what
        // puts exactly one '\n' back if the caller still wants one.
        // Disk-only, same reasoning as ensureFinalNewline itself: only this
        // local copy is touched, never Storage_ (see Editor/TrimOnSave.h).
        if (trimTrailingWhitespace) {
            content = TrimTrailingWhitespaceAndBlankLines(std::move(content));
        }
        // An empty buffer stays empty (not turned into a bare "\n") -- and
        // Storage_ itself is never touched, only this local copy that's about
        // to be written; see the ensureFinalNewline doc comment on the
        // header for why that's deliberate.
        if (ensureFinalNewline) {
            content = EnsureTrailingNewline(std::move(content));
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
    }

    // Truncate-and-write the target's own inode, preserving everything
    // hanging off it (mode, owner, xattrs/ACLs, and every hard link) at the
    // cost of the sibling-temp-then-rename path's crash atomicity. See
    // ExecuteSavePlan below for the two situations that select this.
    void WriteInPlace(const SavePlan& plan) {
        std::ofstream file(plan.target, std::ios::binary | std::ios::trunc);
        if (!file) {
            throw std::runtime_error("ned: cannot open file for writing: " + plan.target.string());
        }

        WriteBufferContent(file, *plan.snapshot, plan.lineEnding, plan.trimTrailingWhitespace, plan.ensureFinalNewline);
        const bool writeFailed = !file;
        file.close();

        if (writeFailed) {
            // Deliberately not removed or restored: unlike the temp-file
            // path, this *is* the real file, and there's no intact copy to
            // fall back to here -- Editor/Backup.h's pre-save version is the
            // recovery route, so the message has to say so rather than imply
            // the file is still intact.
            throw std::runtime_error("ned: error writing file: " + plan.target.string() +
                                     " -- it may now be truncated; recover from a backup version if needed");
        }
    }

} // namespace

void ExecuteSavePlan(const SavePlan& plan) {
    // A multiply-linked file can only stay linked if its own inode is
    // written; a rename would give every other link the stale content. That
    // costs this path's crash atomicity, which is acceptable precisely
    // because save-buffer has already written an Editor/Backup.h version by
    // the time it gets here.
    if (ShouldWriteInPlace(plan.attributes)) {
        WriteInPlace(plan);
        return;
    }

    // Write to a sibling temp file and rename over the target so a failure
    // partway through (e.g. disk full) can't leave the original truncated or
    // corrupted -- std::filesystem::rename is atomic on POSIX when both
    // paths are on the same filesystem, which a sibling file guarantees.
    const std::filesystem::path tempPath = plan.target.string() + ".ned-tmp";

    std::ofstream file(tempPath, std::ios::binary | std::ios::trunc);
    if (!file) {
        // A writable file inside a directory we can't create the temp file
        // in (a read-only source tree with one checked-out file made
        // writable, a directory owned by someone else). The atomic path
        // simply isn't available there, but the save itself still is. Gated
        // on CanCreateSiblingFile rather than on the open failure alone: a
        // temp file can also fail to open because the disk is full or
        // something already occupies its path, and truncating the real file
        // in place for *those* would destroy exactly what this path exists
        // to protect.
        if (plan.attributes.existed && !CanCreateSiblingFile(plan.target)) {
            WriteInPlace(plan);
            return;
        }
        throw std::runtime_error("ned: cannot open file for writing: " + tempPath.string());
    }

    WriteBufferContent(file, *plan.snapshot, plan.lineEnding, plan.trimTrailingWhitespace, plan.ensureFinalNewline);
    const bool writeFailed = !file;
    file.close();

    if (writeFailed) {
        std::filesystem::remove(tempPath);
        throw std::runtime_error("ned: error writing file: " + tempPath.string());
    }

    // Strictly between the write and the rename: the temp file is fully
    // written but not yet visible under the target's name, so nothing ever
    // observes it with the wrong mode.
    ApplyFileAttributes(tempPath, plan.attributes);

    std::error_code ec;
    std::filesystem::rename(tempPath, plan.target, ec);
    if (ec) {
        std::filesystem::remove(tempPath);
        throw std::runtime_error("ned: cannot save file: " + plan.target.string() + " (" + ec.message() + ")");
    }
}

} // namespace ned::text
