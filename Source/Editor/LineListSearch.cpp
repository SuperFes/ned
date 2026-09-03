#include "LineListSearch.h"

#include "Text/Utf8.h"

namespace ned::editor {

namespace {

    char AsciiLower(char c) {
        return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    }

    std::string ToAsciiLower(std::string_view text) {
        std::string result(text);
        for (char& c : result) {
            c = AsciiLower(c);
        }
        return result;
    }

    bool HasAsciiUppercase(std::string_view text) {
        for (const char c : text) {
            if (c >= 'A' && c <= 'Z') {
                return true;
            }
        }
        return false;
    }

} // namespace

LineListSearch::LineListSearch(const std::vector<std::string>& lines, Direction direction, std::size_t startIndex) : lines_(lines), direction_(direction), originalIndex_(lines.empty() ? 0 : startIndex % lines.size()) {
}

void LineListSearch::AppendChar(char32_t codepoint) {
    query_ += text::EncodeCodepointUtf8(codepoint);
    Search(originalIndex_);
}

void LineListSearch::DeleteChar() {
    if (query_.empty()) {
        return;
    }
    text::RemoveLastCodepoint(query_);
    Search(originalIndex_);
}

void LineListSearch::RepeatSearch() {
    if (query_.empty() || lines_.empty()) {
        return;
    }
    const std::size_t from = Advance(currentIndex_.value_or(originalIndex_));
    Search(from);
}

void LineListSearch::ReverseDirection() {
    direction_ = (direction_ == Direction::Forward) ? Direction::Backward : Direction::Forward;
    if (query_.empty() || lines_.empty()) {
        return;
    }
    Search(currentIndex_.value_or(originalIndex_));
}

void LineListSearch::Accept() {
    // Nothing to do -- the caller keeps whatever CurrentIndex() last was.
}

void LineListSearch::Cancel() {
    // Nothing to do -- the caller reads OriginalIndex() to restore its own
    // scroll position.
}

const std::string& LineListSearch::Query() const {
    return query_;
}

bool LineListSearch::Found() const {
    return found_;
}

LineListSearch::Direction LineListSearch::CurrentDirection() const {
    return direction_;
}

std::optional<std::size_t> LineListSearch::CurrentIndex() const {
    return found_ ? currentIndex_ : std::nullopt;
}

std::size_t LineListSearch::OriginalIndex() const {
    return originalIndex_;
}

std::string LineListSearch::StatusText() const {
    std::string text;
    if (!found_) {
        text += "Failing ";
    }
    if (direction_ == Direction::Backward) {
        text += "Backward ";
    }
    text += "I-search: " + query_;
    return text;
}

std::size_t LineListSearch::Advance(std::size_t index) const {
    if (lines_.empty()) {
        return 0;
    }
    if (direction_ == Direction::Forward) {
        return (index + 1) % lines_.size();
    }
    return (index == 0) ? lines_.size() - 1 : index - 1;
}

void LineListSearch::Search(std::size_t from) {
    if (query_.empty()) {
        found_ = true;
        currentIndex_.reset();
        return;
    }
    if (lines_.empty()) {
        found_ = false;
        return;
    }

    from %= lines_.size();

    const bool        caseSensitive = HasAsciiUppercase(query_);
    const std::string needle        = caseSensitive ? query_ : ToAsciiLower(query_);

    std::size_t index = from;
    for (std::size_t step = 0; step < lines_.size(); ++step) {
        const std::string& haystackOwner = lines_[index];
        const std::string  lowered       = caseSensitive ? std::string() : ToAsciiLower(haystackOwner);
        const std::string& haystack      = caseSensitive ? haystackOwner : lowered;
        if (haystack.find(needle) != std::string::npos) {
            found_        = true;
            currentIndex_ = index;
            return;
        }
        index = Advance(index);
    }

    found_ = false; // currentIndex_ stays wherever the last successful match left it
}

} // namespace ned::editor
