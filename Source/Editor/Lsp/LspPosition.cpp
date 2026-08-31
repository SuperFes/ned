#include "LspPosition.h"

#include "Text/ITextStorage.h"

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

} // namespace ned::editor::lsp
