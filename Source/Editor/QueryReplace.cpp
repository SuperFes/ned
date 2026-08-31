#include "QueryReplace.h"

#include <algorithm>

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
// semantics as FindNextMatch above, but reading the subject in bounded
// windows via Content().Substring instead of content_ (empty for a huge
// buffer) -- mirrors IncrementalSearch::SearchHuge's scheme, generalized
// for a regex match rather than a fixed-length literal needle.
//
// Two cursors drive the scan: searchFrom (where the next match is allowed to
// start -- never regresses) and reach (how far past searchFrom the window
// currently extends -- grows on retry, resets once searchFrom itself
// advances). Each window starts kOverlapMargin bytes behind searchFrom, not
// at searchFrom itself, so a genuine match starting inside that margin still
// gets full leading context for lookbehind; the window is always searched
// from searchFrom's own offset within it, so a match can never be reported
// before searchFrom (no separate "discard matches before the cursor" step
// needed) and, just as importantly, the window's own synthetic start is
// never itself a reachable match position -- no ^-at-a-fake-line-start risk
// the way a naive "search the window from its own offset 0" scheme would
// have.
//
// A match found within kOverlapMargin bytes of the window's own end is not
// trusted unless the window reached the real end of the document -- it
// might match differently (or not at all) with more trailing context, a
// multi-line pattern or a lookahead spanning the boundary being the classic
// case. Rather than trust it, reach grows and the same searchFrom is
// retried with a wider window (searchFrom itself must not advance past an
// unconfirmed candidate, or a match starting inside the old window's tail
// margin -- now the new window's lead-in -- would be skipped, since a
// window's own search offset excludes anything before it). Only once a
// window comes up genuinely empty from searchFrom to its own end does
// searchFrom jump forward (to that window's end, with reach reset) -- safe,
// since that range has just been proven to hold no match at all. This
// bounds correctness to "a single match, or the lookaround/multi-line span
// it depends on, is at most kOverlapMargin bytes wide" -- an accepted
// limit, the same class of cut ProjectSearch's line-bounded RE2 path
// already lives with (no lookaround at all there).
void QueryReplace::FindNextMatchHuge() {
    constexpr std::size_t kWindowBody    = 4 * 1024 * 1024;
    constexpr std::size_t kOverlapMargin = 64 * 1024;

    const std::size_t total = buffer_.Content().ByteLength();
    if (searchCursor_ > total) {
        hasMatch_ = false;
        stage_    = Stage::Done;
        return;
    }

    std::size_t searchFrom = searchCursor_;
    std::size_t reach      = kWindowBody;
    for (;;) {
        const std::size_t windowStart = (searchFrom >= kOverlapMargin) ? searchFrom - kOverlapMargin : 0;
        const std::size_t windowEnd   = std::min(total, searchFrom + reach);
        const std::string window      = buffer_.Content().Substring(windowStart, windowEnd - windowStart);

        const bool                      atDocEnd = (windowEnd == total);
        const std::optional<RegexMatch> match    = pattern_->Search(window, searchFrom - windowStart);

        if (match.has_value()) {
            const bool nearTail = !atDocEnd && (window.size() - match->end) < kOverlapMargin;
            if (!nearTail) {
                hasMatch_                  = true;
                matchStart_                = windowStart + match->start;
                matchEnd_                  = windowStart + match->end;
                matchFormattedReplacement_ = pattern_->FormatReplacement(window, *match, replacementText_);
                return;
            }
            reach += kWindowBody; // possibly truncated -- widen and retry from the same searchFrom
            continue;
        }

        if (atDocEnd) {
            break;
        }
        // No match anywhere in [searchFrom, windowEnd) -- safe to skip the
        // whole window; a match can't start earlier than searchFrom (it was
        // excluded from this search) or inside a range just proven empty.
        searchFrom = windowEnd;
        reach      = kWindowBody;
    }

    hasMatch_ = false;
    stage_    = Stage::Done;
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
            return "Replaced " + std::to_string(replacementCount_) + " occurrence" +
                   (replacementCount_ == 1 ? "" : "s") + ".";
    }
    return "";
}

} // namespace ned::editor
