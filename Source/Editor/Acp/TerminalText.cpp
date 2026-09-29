#include "TerminalText.h"

#include "Text/Utf8.h"

namespace ned::editor::acp {

void AppendTerminalText(std::string& out, TerminalTextState& state, std::string_view data) {
    using Mode = TerminalTextState::Mode;
    for (const char c : data) {
        switch (state.mode) {
            case Mode::Escape:
                state.mode = c == '[' ? Mode::Csi : c == ']' ? Mode::Osc
                                                             : Mode::Text;
                continue;
            case Mode::Csi:
                if (c >= 0x40 && c <= 0x7e) {
                    state.mode = Mode::Text;
                }
                continue;
            case Mode::Osc:
                if (c == '\a') {
                    state.mode = Mode::Text;
                }
                else if (c == '\x1b') {
                    state.mode = Mode::OscEscape;
                }
                continue;
            case Mode::OscEscape:
                state.mode = c == '\\' ? Mode::Text : Mode::Osc;
                continue;
            case Mode::Text:
                break;
        }
        if (state.pendingCr) {
            state.pendingCr = false;
            if (c != '\n') {
                // A bare CR: what follows overwrites the line.
                const std::size_t lineStart = out.rfind('\n');
                out.resize(lineStart == std::string::npos ? 0 : lineStart + 1);
            }
        }
        if (c == '\x1b') {
            state.mode = Mode::Escape;
        }
        else if (c == '\r') {
            state.pendingCr = true;
        }
        else if (c == '\n' || c == '\t' || static_cast<unsigned char>(c) >= 0x20) {
            out += c;
        }
    }
}

bool KeepTail(std::string& text, std::size_t maxBytes) {
    if (text.size() <= maxBytes) {
        return false;
    }
    std::size_t cut = text.size() - maxBytes;
    if (const std::size_t newline = text.find('\n', cut); newline != std::string::npos && newline + 1 < text.size()) {
        cut = newline + 1;
    }
    else {
        cut = text::SnapDownToCodepointBoundary(text, cut);
    }
    text.erase(0, cut);
    return true;
}

} // namespace ned::editor::acp
