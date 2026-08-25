//
// Small line/character-classification helpers shared across the Vim subsystem's own
// files (VimMotion.cpp/VimTextObject.cpp/VimEngine.cpp) -- Commands.cpp's own local
// LineContentEnd/GetLineSpan helpers duplicated and named for this subsystem rather than
// exported from Commands.cpp (an editor-command-registration file, not a reusable
// library seam) or added to Buffer itself (these are Vim-specific conveniences over
// Buffer's already-public Rope/line API, the same "pure functions over Buffer's public
// surface" shape Rectangle.h already establishes).
//
// Word/WORD classification is ASCII-only, matching Buffer::MoveForwardWord's own
// documented word-char scope cut -- not Unicode word-boundary-aware.
//

#ifndef NED_EDITOR_VIM_VIMLINEUTIL_H
#define NED_EDITOR_VIM_VIMLINEUTIL_H

#include <cstddef>

#include "Text/Buffer.h"

namespace ned::editor::vim {

[[nodiscard]] inline std::size_t LineOf(const text::Buffer& buffer, std::size_t offset) {
    return buffer.Content().ByteOffsetToLine(offset);
}

[[nodiscard]] inline std::size_t LineCount(const text::Buffer& buffer) {
    return buffer.Content().LineCount();
}

[[nodiscard]] inline std::size_t LineStart(const text::Buffer& buffer, std::size_t line) {
    return buffer.Content().LineToByteOffset(line);
}

// Vim never lets the cursor rest on the phantom trailing empty "line" Rope::LineCount()
// counts when the buffer's last byte is a newline (LineCount() == newline count + 1,
// so a file ending "...text\n" reports one more line than a real vim shows) -- gg/G,
// j's downward clamp, and paragraph motions all want *this* as their real last line, not
// LineCount() - 1. A buffer with no trailing newline, or the single-line empty buffer,
// is unaffected (this returns LineCount() - 1 exactly as before in both those cases).
[[nodiscard]] inline std::size_t EffectiveLastLine(const text::Buffer& buffer) {
    const std::size_t count = LineCount(buffer);
    const std::size_t last  = count - 1;
    if (last > 0 && LineStart(buffer, last) == buffer.Content().ByteLength()) {
        return last - 1;
    }
    return last;
}

[[nodiscard]] inline bool LineHasTrailingNewline(const text::Buffer& buffer, std::size_t line) {
    return line + 1 < LineCount(buffer);
}

// Excludes the line's own trailing newline byte, if it has one -- GetLineSpan's
// contentEnd (Commands.cpp) in a single call.
[[nodiscard]] inline std::size_t LineContentEnd(const text::Buffer& buffer, std::size_t line) {
    return LineHasTrailingNewline(buffer, line) ? LineStart(buffer, line + 1) - 1 : buffer.Content().ByteLength();
}

[[nodiscard]] inline bool IsBlankLine(const text::Buffer& buffer, std::size_t line) {
    return LineStart(buffer, line) == LineContentEnd(buffer, line);
}

// ASCII space/tab -- vim's own notion of "blank" for word/WORD motion and gg/^.
[[nodiscard]] inline bool IsBlankChar(char32_t cp) {
    return cp == U' ' || cp == U'\t';
}

[[nodiscard]] inline bool IsWordChar(char32_t cp) {
    return (cp >= U'a' && cp <= U'z') || (cp >= U'A' && cp <= U'Z') || (cp >= U'0' && cp <= U'9') || cp == U'_';
}

// First non-blank byte offset on line, or LineContentEnd(line) if the whole line (or
// what's left of it) is blank -- "^" and gg/G's own landing column.
[[nodiscard]] inline std::size_t FirstNonBlankOffset(const text::Buffer& buffer, std::size_t line) {
    const std::size_t end    = LineContentEnd(buffer, line);
    std::size_t       offset = LineStart(buffer, line);
    while (offset < end) {
        const auto decoded = buffer.Content().CodepointAt(offset);
        if (!IsBlankChar(decoded.codepoint)) {
            break;
        }
        offset = buffer.Content().NextCodepointBoundary(offset);
    }
    return offset;
}

} // namespace ned::editor::vim

#endif // NED_EDITOR_VIM_VIMLINEUTIL_H
