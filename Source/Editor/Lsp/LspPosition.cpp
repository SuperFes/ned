#include "LspPosition.h"

#include "Text/ITextStorage.h"

#include <algorithm>

namespace ned::editor::lsp {

namespace {

    std::size_t LineByteRangeEnd(const text::ITextStorage& content, std::size_t line) {
        return (line + 1 < content.LineCount()) ? content.LineToByteOffset(line + 1) : content.ByteLength();
    }

} // namespace

LspPosition BytePositionToLsp(const text::ITextStorage& content, std::size_t byteOffset) {
    const std::size_t line      = content.ByteOffsetToLine(byteOffset);
    const std::size_t lineStart = content.LineToByteOffset(line);

    std::size_t utf16Count = 0;
    std::size_t cursor     = lineStart;
    while (cursor < byteOffset) {
        const text::ITextStorage::DecodedCodepoint decoded = content.CodepointAt(cursor);
        utf16Count += (decoded.codepoint > 0xFFFF) ? 2 : 1;
        cursor += decoded.byteLength;
    }

    return LspPosition{.line = line, .character = utf16Count};
}

std::size_t LspPositionToByte(const text::ITextStorage& content, LspPosition position) {
    const std::size_t lineStart        = content.LineToByteOffset(position.line);
    const std::size_t lineEndExclusive = LineByteRangeEnd(content, position.line);

    std::size_t byteOffset = lineStart;
    std::size_t utf16Count = 0;
    while (byteOffset < lineEndExclusive && utf16Count < position.character) {
        const text::ITextStorage::DecodedCodepoint decoded = content.CodepointAt(byteOffset);
        utf16Count += (decoded.codepoint > 0xFFFF) ? 2 : 1;
        byteOffset += decoded.byteLength;
    }
    return byteOffset;
}

namespace {

    // Classifies the UTF-8 sequence starting at content[offset]: how many
    // bytes it occupies and how many UTF-16 code units it decodes to.
    // Mirrors ITextStorage::CodepointAt's own malformed-byte fallback
    // (byteLength always >= 1) without needing the actual decoded codepoint
    // -- only a 4-byte lead byte (a non-BMP codepoint, encoded as a UTF-16
    // surrogate pair) counts as 2 units; everything else (ASCII, 2-byte,
    // 3-byte, or a malformed lead byte treated as one raw byte) is 1.
    struct Utf8Step {
        std::size_t byteLength;
        std::size_t utf16Units;
    };

    Utf8Step StepUtf8(std::string_view content, std::size_t offset) {
        const unsigned char lead = static_cast<unsigned char>(content[offset]);
        std::size_t         len  = 1;
        if ((lead & 0x80) == 0x00) {
            len = 1;
        }
        else if ((lead & 0xE0) == 0xC0) {
            len = 2;
        }
        else if ((lead & 0xF0) == 0xE0) {
            len = 3;
        }
        else if ((lead & 0xF8) == 0xF0) {
            len = 4;
        }
        len = std::min(len, content.size() - offset); // a truncated sequence at the end of content
        return Utf8Step{.byteLength = len, .utf16Units = (len == 4) ? std::size_t{2} : std::size_t{1}};
    }

} // namespace

LspRange ByteRangeToLspRange(std::string_view content, std::size_t startByte, std::size_t endByte) {
    std::size_t line   = 0;
    std::size_t utf16  = 0;
    std::size_t cursor = 0;
    LspPosition start{.line = 0, .character = 0};

    while (cursor < endByte) {
        if (cursor == startByte) {
            start = LspPosition{.line = line, .character = utf16};
        }
        if (content[cursor] == '\n') {
            ++line;
            utf16 = 0;
            cursor += 1;
            continue;
        }
        const Utf8Step step = StepUtf8(content, cursor);
        utf16 += step.utf16Units;
        cursor += step.byteLength;
    }
    if (cursor == startByte) {
        start = LspPosition{.line = line, .character = utf16}; // startByte == endByte
    }

    return LspRange{.start = start, .end = LspPosition{.line = line, .character = utf16}};
}

std::size_t Utf16LengthOfByteRange(std::string_view content, std::size_t startByte, std::size_t endByte) {
    std::size_t utf16  = 0;
    std::size_t cursor = startByte;
    while (cursor < endByte) {
        const Utf8Step step = StepUtf8(content, cursor);
        utf16 += step.utf16Units;
        cursor += step.byteLength;
    }
    return utf16;
}

} // namespace ned::editor::lsp
