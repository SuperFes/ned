#include "MinibufferPrompt.h"

#include <utility>

#include "Text/Utf8.h"

namespace ned::editor {

MinibufferPrompt::MinibufferPrompt(std::string label) : label_(std::move(label)) {
}

void MinibufferPrompt::AppendChar(char32_t codepoint) {
    text_ += text::EncodeCodepointUtf8(codepoint);
}

void MinibufferPrompt::DeleteChar() {
    text::RemoveLastCodepoint(text_);
}

void MinibufferPrompt::SetText(std::string text) {
    text_ = std::move(text);
}

const std::string& MinibufferPrompt::Text() const {
    return text_;
}

std::string MinibufferPrompt::StatusText() const {
    return label_ + text_;
}

} // namespace ned::editor
