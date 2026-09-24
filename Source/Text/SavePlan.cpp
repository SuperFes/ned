#include "SavePlan.h"

#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>

#include "WhitespaceHygiene.h"

namespace ned::text {

namespace {
    // Where every byte of a save goes: straight to the file for the UTF-8
    // family, through the charset's encoder for anything else. A character
    // the encoder refuses fails the stream, which each write mode already
    // treats as a failed write -- ValidateSavePlanCharset is what keeps
    // that from being reached.
    class EncodingSink {
      public:
        EncodingSink(std::ofstream& file, Charset charset) : file_(file), encoder_(charset), passThrough_(IsUtf8Family(charset)) {
        }

        void Write(const char* data, std::size_t size) {
            if (passThrough_) {
                file_.write(data, static_cast<std::streamsize>(size));
                return;
            }
            encoded_.clear();
            if (!encoder_.Feed(std::string_view(data, size), encoded_)) {
                file_.setstate(std::ios::failbit);
            }
            file_.write(encoded_.data(), static_cast<std::streamsize>(encoded_.size()));
        }

        void Finish() {
            if (!encoder_.Finish()) {
                file_.setstate(std::ios::failbit);
            }
        }

      private:
        std::ofstream& file_;
        CharsetEncoder encoder_;
        bool           passThrough_;
        std::string    encoded_;
    };

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
        StreamingSaveWriter(EncodingSink& file, LineEnding ending, bool trim, bool ensureFinalNewline,
                            const std::function<void(std::uintmax_t)>& onProgress) :
            file_(file), ending_(ending), trim_(trim), ensureFinalNewline_(ensureFinalNewline), onProgress_(onProgress) {
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
                file_.Write(outBuffer_.data(), outBuffer_.size());
                bytesWritten_ += outBuffer_.size();
                outBuffer_.clear();
                if (onProgress_) {
                    onProgress_(bytesWritten_);
                }
            }
        }

        static constexpr std::size_t kFlushThreshold = 256 * 1024;

        EncodingSink&                              file_;
        LineEnding                                 ending_;
        bool                                       trim_;
        bool                                       ensureFinalNewline_;
        const std::function<void(std::uintmax_t)>& onProgress_;
        std::uintmax_t                             bytesWritten_ = 0;

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
    void WriteBufferContent(EncodingSink& file, const ITextStorage& storage, LineEnding effectiveEnding, bool trimTrailingWhitespace,
                            bool ensureFinalNewline, const std::function<void(std::uintmax_t)>& onProgress) {
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
            StreamingSaveWriter writer(file, effectiveEnding, trimTrailingWhitespace, ensureFinalNewline, onProgress);
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

        // Written in chunks rather than one call purely so a save large
        // enough to run off the main thread can report progress while it
        // does; the bytes and their order are identical either way.
        constexpr std::size_t kWriteChunkBytes = 256 * 1024;
        std::uintmax_t        written          = 0;
        for (std::size_t offset = 0; offset < content.size(); offset += kWriteChunkBytes) {
            const std::size_t count = std::min(kWriteChunkBytes, content.size() - offset);
            file.Write(content.data() + offset, count);
            written += count;
            if (onProgress) {
                onProgress(written);
            }
        }
    }

    // Truncate-and-write the target's own inode, preserving everything
    // hanging off it (mode, owner, xattrs/ACLs, and every hard link) at the
    // cost of the sibling-temp-then-rename path's crash atomicity. See
    // ExecuteSavePlan below for the two situations that select this.
    void WritePlanContent(std::ofstream& file, const SavePlan& plan) {
        const std::string_view preamble = CharsetPreamble(plan.charset);
        file.write(preamble.data(), static_cast<std::streamsize>(preamble.size()));
        EncodingSink sink(file, plan.charset);
        WriteBufferContent(sink, *plan.snapshot, plan.lineEnding, plan.trimTrailingWhitespace, plan.ensureFinalNewline,
                           plan.onProgress);
        sink.Finish();
    }

    void WriteInPlace(const SavePlan& plan) {
        std::ofstream file(plan.target, std::ios::binary | std::ios::trunc);
        if (!file) {
            throw std::runtime_error("ned: cannot open file for writing: " + plan.target.string());
        }

        WritePlanContent(file, plan);
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

void ValidateSavePlanCharset(const SavePlan& plan) {
    if (IsUtf8Family(plan.charset)) {
        return;
    }
    CharsetEncoder encoder(plan.charset);
    std::string    discard;
    bool           encodable = true;
    plan.snapshot->ForEachChunk([&](std::string_view chunk) {
        discard.clear();
        encodable = encodable && encoder.Feed(chunk, discard);
    });
    if (encodable && encoder.Finish()) {
        return;
    }

    const ITextStorage& storage = *plan.snapshot;
    const std::size_t   offset  = encoder.FailedAt();
    const std::size_t   line    = storage.ByteOffsetToLine(offset);
    const std::string   before  = storage.Substring(storage.LineToByteOffset(line), offset - storage.LineToByteOffset(line));
    const std::size_t   column  = static_cast<std::size_t>(
        std::ranges::count_if(before, [](char c) { return (static_cast<unsigned char>(c) & 0xC0) != 0x80; }));
    throw std::runtime_error("ned: can't save " + plan.target.string() + " as " + std::string(CharsetName(plan.charset)) +
                             ": line " + std::to_string(line + 1) + ", column " + std::to_string(column + 1) +
                             " has a character it can't hold -- set-buffer-charset picks another");
}

void ExecuteSavePlan(const SavePlan& plan) {
    ValidateSavePlanCharset(plan);
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

    WritePlanContent(file, plan);
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
