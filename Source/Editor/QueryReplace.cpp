#include "QueryReplace.h"

#include "Text/Utf8.h"

namespace ned::editor {

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
    content_      = buffer_.Text();
    searchCursor_ = buffer_.Point();
    stage_        = Stage::Confirming;
    FindNextMatch();
}

void QueryReplace::FindNextMatch() {
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

void QueryReplace::ReplaceAndNext() {
    if (stage_ != Stage::Confirming || !hasMatch_) {
        return;
    }

    const bool wasEmptyMatch = (matchStart_ == matchEnd_);

    buffer_.DeleteRange(matchStart_, matchEnd_ - matchStart_);
    buffer_.InsertAt(matchStart_, matchFormattedReplacement_);
    content_.replace(matchStart_, matchEnd_ - matchStart_, matchFormattedReplacement_);
    ++replacementCount_;

    searchCursor_ = matchStart_ + matchFormattedReplacement_.size();
    if (wasEmptyMatch && matchFormattedReplacement_.empty()) {
        // Guarantee forward progress against a zero-width match with an
        // empty replacement -- one whole codepoint (the pattern runs in UTF
        // mode), or past the end to terminate.
        searchCursor_ = (searchCursor_ >= content_.size()) ? content_.size() + 1
                                                           : text::NextCodepointBoundary(content_, searchCursor_);
    }

    FindNextMatch();
}

void QueryReplace::SkipAndNext() {
    if (stage_ != Stage::Confirming || !hasMatch_) {
        return;
    }
    if (matchStart_ == matchEnd_) { // zero-width: step one codepoint, or past the end to terminate
        searchCursor_ =
            (matchEnd_ >= content_.size()) ? content_.size() + 1 : text::NextCodepointBoundary(content_, matchEnd_);
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
