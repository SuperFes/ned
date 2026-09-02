#include "MinibufferPrompt.h"

#include <string_view>
#include <utility>

#include "Text/Utf8.h"

namespace ned::editor {

namespace {

    // One column per codepoint, matching PaintUtf8Row's own convention (no
    // double-width CJK/emoji handling anywhere in this codebase yet -- see
    // ROADMAP.md).
    int DisplayColumns(std::string_view text) {
        int         columns = 0;
        std::size_t pos     = 0;
        while (pos < text.size()) {
            pos = text::NextCodepointBoundary(text, pos);
            ++columns;
        }
        return columns;
    }

    // ASCII alphanumeric + underscore -- deliberately not Unicode-aware,
    // mirroring Buffer.cpp's private IsWordCodepoint exactly (same
    // "one-line predicate private to each consumer" convention AcpPanel.cpp's
    // own IsPlainCharacter comment documents, rather than a new shared
    // utility for one three-line check). Safe to test a single byte at a
    // codepoint boundary: a UTF-8 continuation byte is always >= 0x80 and a
    // multi-byte lead byte is too, so neither can ever match this range
    // (WordWrap's own `text[pos] == ' '` check in AcpPanel.cpp relies on the
    // same fact).
    bool IsWordByte(char ch) {
        const unsigned char uch = static_cast<unsigned char>(ch);
        return (uch >= 'a' && uch <= 'z') || (uch >= 'A' && uch <= 'Z') || (uch >= '0' && uch <= '9') || uch == '_';
    }

} // namespace

MinibufferPrompt::MinibufferPrompt(std::string label) : label_(std::move(label)) {
}

void MinibufferPrompt::InsertChar(char32_t codepoint) {
    const std::string encoded = text::EncodeCodepointUtf8(codepoint);
    text_.insert(cursor_, encoded);
    cursor_ += encoded.size();
}

void MinibufferPrompt::DeleteBackward() {
    if (cursor_ == 0) {
        return;
    }
    const std::size_t start = text::PreviousCodepointBoundary(text_, cursor_);
    text_.erase(start, cursor_ - start);
    cursor_ = start;
}

void MinibufferPrompt::DeleteForward() {
    if (cursor_ >= text_.size()) {
        return;
    }
    const std::size_t end = text::NextCodepointBoundary(text_, cursor_);
    text_.erase(cursor_, end - cursor_);
}

void MinibufferPrompt::MoveCursorLeft() {
    cursor_ = text::PreviousCodepointBoundary(text_, cursor_);
}

void MinibufferPrompt::MoveCursorRight() {
    cursor_ = text::NextCodepointBoundary(text_, cursor_);
}

void MinibufferPrompt::MoveCursorWordLeft() {
    // Skip any run of non-word bytes immediately behind the cursor, then the
    // word run behind that -- Buffer::MoveBackwardWord's exact two-phase
    // structure, just walking a plain std::string instead of a Rope.
    while (cursor_ > 0) {
        const std::size_t previous = text::PreviousCodepointBoundary(text_, cursor_);
        if (IsWordByte(text_[previous])) {
            break;
        }
        cursor_ = previous;
    }
    while (cursor_ > 0) {
        const std::size_t previous = text::PreviousCodepointBoundary(text_, cursor_);
        if (!IsWordByte(text_[previous])) {
            break;
        }
        cursor_ = previous;
    }
}

void MinibufferPrompt::MoveCursorWordRight() {
    while (cursor_ < text_.size() && !IsWordByte(text_[cursor_])) {
        cursor_ = text::NextCodepointBoundary(text_, cursor_);
    }
    while (cursor_ < text_.size() && IsWordByte(text_[cursor_])) {
        cursor_ = text::NextCodepointBoundary(text_, cursor_);
    }
}

void MinibufferPrompt::MoveCursorToStart() {
    cursor_ = 0;
}

void MinibufferPrompt::MoveCursorToEnd() {
    cursor_ = text_.size();
}

void MinibufferPrompt::SetText(std::string text) {
    text_   = std::move(text);
    cursor_ = text_.size();
}

const std::string& MinibufferPrompt::Text() const {
    return text_;
}

std::string MinibufferPrompt::StatusText() const {
    return label_ + text_;
}

std::size_t MinibufferPrompt::CursorByteOffset() const {
    return cursor_;
}

int MinibufferPrompt::CursorDisplayColumn() const {
    return DisplayColumns(label_) + DisplayColumns(std::string_view(text_).substr(0, cursor_));
}

} // namespace ned::editor
