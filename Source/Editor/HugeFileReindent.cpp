#include "HugeFileReindent.h"

#include <algorithm>
#include <utility>

#include "Indent.h"

namespace ned::editor {

namespace {
    constexpr std::size_t kFlushThreshold = 256 * 1024; // Buffer.cpp's StreamingSaveWriter's own threshold
} // namespace

HugeReindentStream::HugeReindentStream(std::ofstream& out, std::string lineCommentPrefix, IndentStyle style)
    : out_(out), lineCommentPrefix_(std::move(lineCommentPrefix)), style_(style) {
}

void HugeReindentStream::operator()(std::string_view chunk) {
    for (const char c : chunk) {
        if (failed_) {
            return;
        }
        Feed(c);
    }
}

void HugeReindentStream::Feed(char c) {
    if (inLineComment_) {
        WriteByte(c);
        if (c == '\n') {
            inLineComment_ = false;
            atLineStart_   = true;
            recentBytes_.clear();
        }
        return;
    }

    if (inString_) {
        WriteByte(c);
        if (escapeNext_) {
            escapeNext_ = false;
        }
        else if (c == '\\') {
            escapeNext_ = true;
        }
        else if (c == stringQuote_) {
            inString_ = false;
        }
        return;
    }

    if (atLineStart_) {
        if (c == ' ' || c == '\t') {
            originalIndent_.push_back(c);
            return;
        }
        if (c == '\n') {
            // A genuinely blank (or all-whitespace) line -- no indent to
            // emit, matching every other Hygiene-shaped rule in this
            // codebase that never adds trailing whitespace to a blank line.
            WriteByte(c);
            originalIndent_.clear();
            return;
        }
        const bool isCloser = (c == ')' || c == '}' || c == ']');
        EmitIndentForNewLine(isCloser);
        atLineStart_ = false;
        // Falls through -- c itself still needs the ordinary processing
        // below (it may be a bracket, quote, or the start of a comment).
    }

    if (!lineCommentPrefix_.empty()) {
        recentBytes_.push_back(c);
        if (recentBytes_.size() > lineCommentPrefix_.size()) {
            recentBytes_.erase(0, recentBytes_.size() - lineCommentPrefix_.size());
        }
        if (recentBytes_ == lineCommentPrefix_) {
            WriteByte(c);
            inLineComment_ = true;
            recentBytes_.clear();
            return;
        }
    }

    if (c == '"' || c == '\'') {
        inString_    = true;
        stringQuote_ = c;
        WriteByte(c);
        return;
    }

    if (c == '(' || c == '{' || c == '[') {
        ++depth_;
        WriteByte(c);
        return;
    }
    if (c == ')' || c == '}' || c == ']') {
        --depth_;
        if (depth_ < 0) {
            WriteByte(c);
            Fail("more closing delimiters than opening ones");
            return;
        }
        WriteByte(c);
        return;
    }

    if (c == '\n') {
        WriteByte(c);
        atLineStart_ = true;
        recentBytes_.clear();
        return;
    }

    WriteByte(c);
}

void HugeReindentStream::EmitIndentForNewLine(bool firstByteIsCloser) {
    const long long   lineStartDepth = depth_ - (firstByteIsCloser ? 1 : 0);
    const int         column         = static_cast<int>(std::max<long long>(0, lineStartDepth)) * style_.width;
    const std::string newIndent      = IndentString(column, style_);
    if (newIndent != originalIndent_) {
        ++linesChanged_;
    }
    for (const char c : newIndent) {
        WriteByte(c);
    }
    originalIndent_.clear();
}

void HugeReindentStream::WriteByte(char c) {
    outBuffer_.push_back(c);
    MaybeFlush();
}

void HugeReindentStream::MaybeFlush() {
    if (outBuffer_.size() >= kFlushThreshold) {
        FlushBuffer();
    }
}

void HugeReindentStream::FlushBuffer() {
    if (!outBuffer_.empty()) {
        out_.write(outBuffer_.data(), static_cast<std::streamsize>(outBuffer_.size()));
        outBuffer_.clear();
    }
}

void HugeReindentStream::Fail(std::string reason) {
    failed_        = true;
    failureReason_ = std::move(reason);
}

HugeReindentOutcome HugeReindentStream::Finish() {
    if (!failed_) {
        if (inString_) {
            Fail("unterminated string literal");
        }
        else if (depth_ != 0) {
            Fail("unbalanced delimiters (" + std::to_string(depth_) + (depth_ > 0 ? " left open)" : " too many closed)"));
        }
    }
    FlushBuffer();
    if (failed_) {
        return HugeReindentOutcome{false, linesChanged_, failureReason_};
    }
    return HugeReindentOutcome{true, linesChanged_, {}};
}

HugeReindentOutcome StreamHugeReindent(const text::ITextStorage& source, std::ofstream& out,
                                       const std::string& lineCommentPrefix, const IndentStyle& style) {
    HugeReindentStream stream(out, lineCommentPrefix, style);
    source.ForEachChunk([&stream](std::string_view chunk) { stream(chunk); });
    return stream.Finish();
}

} // namespace ned::editor
