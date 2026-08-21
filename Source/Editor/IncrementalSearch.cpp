#include "IncrementalSearch.h"

#include "Text/Utf8.h"

namespace ned::editor {

namespace {

    // ASCII alphanumeric + underscore -- deliberately not Unicode-aware,
    // matching Buffer::MoveForwardWord's own word-char definition (see its
    // doc comment in Buffer.h) since content_ has no codepoint-decoding
    // available to it, only raw UTF-8 bytes.
    bool IsWordByte(char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
    }

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

IncrementalSearch::IncrementalSearch(text::Buffer& buffer, Direction direction)
    : buffer_(buffer), direction_(direction), content_(buffer.Text()), contentLower_(ToAsciiLower(content_)),
      originalPoint_(buffer.Point()) {}

void IncrementalSearch::AppendChar(char32_t codepoint) {
    query_ += text::EncodeCodepointUtf8(codepoint);
    Search(originalPoint_);
}

void IncrementalSearch::DeleteChar() {
    if (query_.empty()) {
        return;
    }
    text::RemoveLastCodepoint(query_);
    Search(originalPoint_);
}

void IncrementalSearch::AppendText(std::string_view text) {
    if (text.empty()) {
        return;
    }
    query_ += text;
    Search(originalPoint_);
}

void IncrementalSearch::AppendWordAtPoint() {
    std::size_t       offset = buffer_.Point();
    const std::size_t total  = content_.size();

    while (offset < total && !IsWordByte(content_[offset])) {
        ++offset;
    }
    const std::size_t start = offset;
    while (offset < total && IsWordByte(content_[offset])) {
        ++offset;
    }
    if (offset == start) {
        return; // no word-shaped text left in this direction
    }
    AppendText(std::string_view(content_).substr(start, offset - start));
}

void IncrementalSearch::RepeatSearch() {
    if (query_.empty()) {
        return;
    }
    Search(buffer_.Point());
}

void IncrementalSearch::ReverseDirection() {
    direction_ = (direction_ == Direction::Forward) ? Direction::Backward : Direction::Forward;
    if (query_.empty()) {
        return;
    }
    Search(buffer_.Point());
}

void IncrementalSearch::Accept() {
    // Nothing to do here -- point is already at the current match (or
    // unchanged if the query never matched). The session simply ends when
    // the caller discards this object.
}

void IncrementalSearch::Cancel() {
    buffer_.SetPoint(originalPoint_);
}

const std::string& IncrementalSearch::Query() const {
    return query_;
}

bool IncrementalSearch::Found() const {
    return found_;
}

std::string IncrementalSearch::StatusText() const {
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

void IncrementalSearch::Search(std::size_t from) {
    if (query_.empty()) {
        found_ = true;
        buffer_.SetPoint(originalPoint_);
        return;
    }

    // Smart case, Emacs-style: case-insensitive unless the query itself
    // contains an uppercase letter (see this file's header comment).
    const bool          caseSensitive = HasAsciiUppercase(query_);
    const std::string&  haystack      = caseSensitive ? content_ : contentLower_;
    const std::string   needleOwner   = caseSensitive ? std::string() : ToAsciiLower(query_);
    const std::string&  needle        = caseSensitive ? query_ : needleOwner;

    std::size_t pos;
    if (direction_ == Direction::Forward) {
        pos = haystack.find(needle, from);
        if (pos == std::string::npos) {
            pos = haystack.find(needle); // wrap around to the top of the document
        }
    } else {
        pos = (from == 0) ? std::string::npos : haystack.rfind(needle, from - 1);
        if (pos == std::string::npos) {
            pos = haystack.rfind(needle); // wrap around to the bottom of the document
        }
    }

    if (pos == std::string::npos) {
        found_ = false;
        return; // point stays wherever the last successful match left it
    }

    found_ = true;
    buffer_.SetPoint(direction_ == Direction::Forward ? pos + query_.size() : pos);
}

} // namespace ned::editor
