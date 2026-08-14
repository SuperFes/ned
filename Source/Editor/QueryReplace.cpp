#include "QueryReplace.h"

#include "Text/Utf8.h"

namespace ned::editor {

QueryReplace::QueryReplace(text::Buffer& buffer) : buffer_(buffer) {}

void QueryReplace::AppendChar(char32_t codepoint) {
    if (stage_ == Stage::EnteringPattern) {
        patternText_ += text::EncodeCodepointUtf8(codepoint);
    } else if (stage_ == Stage::EnteringReplacement) {
        replacementText_ += text::EncodeCodepointUtf8(codepoint);
    }
}

void QueryReplace::DeleteChar() {
    if (stage_ == Stage::EnteringPattern) {
        text::RemoveLastCodepoint(patternText_);
    } else if (stage_ == Stage::EnteringReplacement) {
        text::RemoveLastCodepoint(replacementText_);
    }
}

void QueryReplace::ConfirmPattern() {
    if (stage_ != Stage::EnteringPattern || patternText_.empty()) {
        return;
    }
    pattern_ = std::regex(patternText_); // throws std::regex_error on invalid syntax
    stage_   = Stage::EnteringReplacement;
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
    std::smatch match;
    if (searchCursor_ > content_.size() ||
        !std::regex_search(content_.cbegin() + static_cast<std::ptrdiff_t>(searchCursor_), content_.cend(), match, pattern_)) {
        hasMatch_ = false;
        stage_    = Stage::Done;
        return;
    }

    hasMatch_                  = true;
    matchStart_                = searchCursor_ + static_cast<std::size_t>(match.position(0));
    matchEnd_                  = matchStart_ + static_cast<std::size_t>(match.length(0));
    matchFormattedReplacement_ = match.format(replacementText_);
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
        ++searchCursor_; // guarantee forward progress against a zero-width match with an empty replacement
    }

    FindNextMatch();
}

void QueryReplace::SkipAndNext() {
    if (stage_ != Stage::Confirming || !hasMatch_) {
        return;
    }
    searchCursor_ = (matchStart_ == matchEnd_) ? matchEnd_ + 1 : matchEnd_;
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
