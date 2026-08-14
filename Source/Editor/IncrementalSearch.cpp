#include "IncrementalSearch.h"

#include "Text/Utf8.h"

namespace ned::editor {

IncrementalSearch::IncrementalSearch(text::Buffer& buffer, Direction direction)
    : buffer_(buffer), direction_(direction), content_(buffer.Text()), originalPoint_(buffer.Point()) {}

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

void IncrementalSearch::RepeatSearch() {
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

    std::size_t pos;
    if (direction_ == Direction::Forward) {
        pos = content_.find(query_, from);
    } else {
        pos = (from == 0) ? std::string::npos : content_.rfind(query_, from - 1);
    }

    if (pos == std::string::npos) {
        found_ = false;
        return; // point stays wherever the last successful match left it
    }

    found_ = true;
    buffer_.SetPoint(direction_ == Direction::Forward ? pos + query_.size() : pos);
}

} // namespace ned::editor
