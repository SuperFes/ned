//
// LSP client follow-up. LSP's own line/UTF-16-code-unit position shape,
// factored out of LspManager.cpp's original diagnostics-only, file-local
// helpers so hover/completion requests (which need the reverse direction --
// buffer byte offset -> LSP Position, not just LSP Position -> byte offset)
// can share the exact same conversion logic rather than re-deriving it.
//

#ifndef NED_EDITOR_LSP_LSPPOSITION_H
#define NED_EDITOR_LSP_LSPPOSITION_H

#include <cstddef>
#include <string_view>

namespace ned::text {
class ITextStorage;
} // namespace ned::text

namespace ned::editor::lsp {

// character is a UTF-16 code-unit offset within the line, not a byte or
// codepoint offset -- LSP's own Position shape (see the "specification"
// section of the LSP spec on "Position").
struct LspPosition {
    std::size_t line;
    std::size_t character;

    bool operator==(const LspPosition&) const = default;
};

// Converts a buffer byte offset to an LSP line/UTF-16-character Position.
[[nodiscard]] LspPosition BytePositionToLsp(const text::ITextStorage& content, std::size_t byteOffset);

// Converts an LSP line/UTF-16-character Position back to a buffer byte
// offset. Bounded by the line's own byte range, so a malformed/out-of-range
// server-reported character can't walk off the end of the line.
[[nodiscard]] std::size_t LspPositionToByte(const text::ITextStorage& content, LspPosition position);

// incremental-sync follow-up. contentChanges[0].range's shape for an
// incremental textDocument/didChange.
struct LspRange {
    LspPosition start;
    LspPosition end;

    bool operator==(const LspRange&) const = default;
};

// Converts a [startByte, endByte) span within content to an LspRange.
// Unlike BytePositionToLsp/LspPositionToByte above, this walks content
// directly rather than through ITextStorage: incremental sync's two
// operands are a frozen "last synced text" snapshot and a freshly
// materialized "new text" (which, for an embedded virtual document, is
// never the same string as any live buffer's own storage at all) -- plain
// strings with no ITextStorage wrapper, and building a real
// RopeStorage/PieceTableStorage just to convert one range would be a
// needless tree-build allocation on every sync. startByte and endByte must
// satisfy startByte <= endByte <= content.size().
[[nodiscard]] LspRange ByteRangeToLspRange(std::string_view content, std::size_t startByte, std::size_t endByte);

// UTF-16 code-unit length of content[startByte, endByte) --
// contentChanges[0].rangeLength, deprecated by the LSP spec but still
// required by some servers. Same content contract as ByteRangeToLspRange.
[[nodiscard]] std::size_t Utf16LengthOfByteRange(std::string_view content, std::size_t startByte, std::size_t endByte);

} // namespace ned::editor::lsp

#endif // NED_EDITOR_LSP_LSPPOSITION_H
