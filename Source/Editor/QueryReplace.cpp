#include "QueryReplace.h"

#include <algorithm>

#include "HugeRegexScan.h"
#include "Text/Utf8.h"

namespace ned::editor {

namespace {

    // huge-file-regex-replace follow-up: NextCodepointBoundary needs a few
    // bytes of context to walk past UTF-8 continuation bytes; a UTF-8
    // sequence is at most 4 bytes, so a small bounded read from the buffer
    // (rather than content_, which is empty for a huge buffer) is enough.
    std::size_t NextCodepointBoundaryInBuffer(const text::Buffer& buffer, std::size_t offset) {
        const std::size_t total = buffer.Content().ByteLength();
        if (offset >= total) {
            return total;
        }
        const std::string window = buffer.Content().Substring(offset, std::min<std::size_t>(4, total - offset));
        return offset + text::NextCodepointBoundary(window, 0);
    }

} // namespace

QueryReplace::QueryReplace(text::Buffer& buffer) : buffer_(buffer) {
}

void QueryReplace::AppendChar(char32_t codepoint) {
    if (stage_ == Stage::EnteringPattern) {
        patternText_ += text::EncodeCodepointUtf8(codepoint);
    }
    else if (stage_ == Stage::EnteringReplacement) {
        replacementText_ += text::EncodeCodepointUtf8(codepoint);
    }
}

void QueryReplace::DeleteChar() {
    if (stage_ == Stage::EnteringPattern) {
        text::RemoveLastCodepoint(patternText_);
    }
    else if (stage_ == Stage::EnteringReplacement) {
        text::RemoveLastCodepoint(replacementText_);
    }
}

void QueryReplace::ConfirmPattern() {
    if (stage_ != Stage::EnteringPattern || patternText_.empty()) {
        return;
    }
    pattern_.emplace(patternText_); // throws RegexPatternError on invalid syntax, leaving pattern_ empty
    stage_ = Stage::EnteringReplacement;
}

void QueryReplace::ConfirmReplacement() {
    if (stage_ != Stage::EnteringReplacement) {
        return;
    }
    // binary-safety-guardrails follow-up: the replace step writes bytes back
    // into the buffer, same class of "byte-level, content-changing" action
    // BinarySafeguardsActive()'s other guard sites (format-on-save,
    // convert-line-endings, ...) already refuse for a confirmed-binary
    // buffer -- refused outright here too, mirroring their exact wording
    // convention, rather than silently matching/replacing raw binary bytes
    // as if they were text.
    if (buffer_.BinarySafeguardsActive()) {
        binaryRefused_ = true;
        stage_         = Stage::Done;
        return;
    }
    huge_ = buffer_.Content().IsHuge();
    if (!huge_) {
        content_ = buffer_.Text();
    }
    searchCursor_ = buffer_.Point();
    stage_        = Stage::Confirming;
    FindNextMatch();
}

void QueryReplace::FindNextMatch() {
    if (huge_) {
        FindNextMatchHuge();
        return;
    }

    // Searches the whole content from an offset (not a trimmed subrange), so
    // ^/\b/lookbehind correctly see what precedes the cursor -- see
    // RegexPattern.h.
    std::optional<RegexMatch> match;
    if (searchCursor_ <= content_.size()) {
        match = pattern_->Search(content_, searchCursor_);
    }
    if (!match.has_value()) {
        hasMatch_ = false;
        stage_    = Stage::Done;
        return;
    }

    hasMatch_                  = true;
    matchStart_                = match->start;
    matchEnd_                  = match->end;
    matchFormattedReplacement_ = pattern_->FormatReplacement(content_, *match, replacementText_);
}

// huge-file-regex-replace follow-up: same forward-only, no-wraparound
// semantics as FindNextMatch above, but delegating to the windowed scan
// shared with Vim mode's search (Editor/HugeRegexScan.h) instead of
// materializing content_ (empty for a huge buffer).
void QueryReplace::FindNextMatchHuge() {
    const std::optional<HugeRegexMatch> found = FindNextRegexMatchHuge(buffer_, *pattern_, searchCursor_);
    if (!found.has_value()) {
        hasMatch_ = false;
        stage_    = Stage::Done;
        return;
    }

    hasMatch_                  = true;
    matchStart_                = found->windowStart + found->match.start;
    matchEnd_                  = found->windowStart + found->match.end;
    matchFormattedReplacement_ = pattern_->FormatReplacement(found->window, found->match, replacementText_);
}

void QueryReplace::ReplaceAndNext() {
    if (stage_ != Stage::Confirming || !hasMatch_) {
        return;
    }

    const bool wasEmptyMatch = (matchStart_ == matchEnd_);

    buffer_.DeleteRange(matchStart_, matchEnd_ - matchStart_);
    buffer_.InsertAt(matchStart_, matchFormattedReplacement_);
    if (!huge_) {
        content_.replace(matchStart_, matchEnd_ - matchStart_, matchFormattedReplacement_);
    }
    ++replacementCount_;

    searchCursor_ = matchStart_ + matchFormattedReplacement_.size();
    if (wasEmptyMatch && matchFormattedReplacement_.empty()) {
        // Guarantee forward progress against a zero-width match with an
        // empty replacement -- one whole codepoint (the pattern runs in UTF
        // mode), or past the end to terminate.
        if (huge_) {
            const std::size_t total = buffer_.Content().ByteLength();
            searchCursor_ =
                (searchCursor_ >= total) ? total + 1 : NextCodepointBoundaryInBuffer(buffer_, searchCursor_);
        }
        else {
            searchCursor_ = (searchCursor_ >= content_.size()) ? content_.size() + 1
                                                               : text::NextCodepointBoundary(content_, searchCursor_);
        }
    }

    FindNextMatch();
}

void QueryReplace::SkipAndNext() {
    if (stage_ != Stage::Confirming || !hasMatch_) {
        return;
    }
    if (matchStart_ == matchEnd_) { // zero-width: step one codepoint, or past the end to terminate
        if (huge_) {
            const std::size_t total = buffer_.Content().ByteLength();
            searchCursor_           = (matchEnd_ >= total) ? total + 1 : NextCodepointBoundaryInBuffer(buffer_, matchEnd_);
        }
        else {
            searchCursor_ = (matchEnd_ >= content_.size()) ? content_.size() + 1
                                                           : text::NextCodepointBoundary(content_, matchEnd_);
        }
    }
    else {
        searchCursor_ = matchEnd_;
    }
    FindNextMatch();
}

void QueryReplace::ReplaceAll() {
    if (stage_ != Stage::Confirming) {
        return;
    }
    while (hasMatch_ && stage_ == Stage::Confirming) {
        ReplaceAndNext();
    }
}

void QueryReplace::Finish() {
    stage_ = Stage::Done;
}

void QueryReplace::Cancel() {
    stage_ = Stage::Done;
}

QueryReplace::Stage QueryReplace::CurrentStage() const {
    return stage_;
}

std::size_t QueryReplace::ReplacementCount() const {
    return replacementCount_;
}

std::string QueryReplace::StatusText() const {
    switch (stage_) {
        case Stage::EnteringPattern:
            return "Query replace: " + patternText_;
        case Stage::EnteringReplacement:
            return "Query replace " + patternText_ + " with: " + replacementText_;
        case Stage::Confirming:
            return hasMatch_ ? "Query replacing " + patternText_ + " with " + replacementText_ + " (y/n/!/q)?"
                             : "No more matches.";
        case Stage::Done:
            if (binaryRefused_) {
                return "\"" + buffer_.Name() +
                       "\" looks like binary content -- refusing to query-replace it (run "
                       "toggle-binary-safeguards to override)";
            }
            return "Replaced " + std::to_string(replacementCount_) + " occurrence" +
                   (replacementCount_ == 1 ? "" : "s") + ".";
    }
    return "";
}

} // namespace ned::editor
