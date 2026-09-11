//
// LSP client follow-up. LSP's own line/UTF-16-code-unit position shape,
// factored out of Manager.cpp's original diagnostics-only, file-local
// helpers so hover/completion requests (which need the reverse direction --
// buffer byte offset -> LSP Position, not just LSP Position -> byte offset)
// can share the exact same conversion logic rather than re-deriving it.
//

#ifndef NED_EDITOR_LSP_POSITION_H
#define NED_EDITOR_LSP_POSITION_H

#include <cstddef>
#include <string_view>

namespace ned::text {
class ITextStorage;
} // namespace ned::text

namespace ned::editor::lsp {

// character is a UTF-16 code-unit offset within the line, not a byte or
// codepoint offset -- LSP's own Position shape (see the "specification"
// section of the LSP spec on "Position").
struct Position {
    // Both default-initialized: several types hold an Position by value
    // and are themselves default-constructed before being conditionally
    // filled in (DocumentLink's range when the server sent none,
    // WorkspaceTextEdit inside an unset CompletionItem::textEdit). Without
    // these, "the default is 0:0" -- which those call sites and their tests
    // both rely on -- is an indeterminate read that only happens to hold
    // under whatever the stack last contained.
    std::size_t line      = 0;
    std::size_t character = 0;

    bool operator==(const Position&) const = default;
};

// Converts a buffer byte offset to an LSP line/UTF-16-character Position.
[[nodiscard]] Position BytePositionToLsp(const text::ITextStorage& content, std::size_t byteOffset);

// Converts an LSP line/UTF-16-character Position back to a buffer byte
// offset. Bounded by the line's own byte range, so a malformed/out-of-range
// server-reported character can't walk off the end of the line.
[[nodiscard]] std::size_t PositionToByte(const text::ITextStorage& content, Position position);

// incremental-sync follow-up. contentChanges[0].range's shape for an
// incremental textDocument/didChange.
struct Range {
    Position start;
    Position end;

    bool operator==(const Range&) const = default;
};

// Converts a [startByte, endByte) span within content to an Range.
// Unlike BytePositionToLsp/PositionToByte above, this walks content
// directly rather than through ITextStorage: incremental sync's two
// operands are a frozen "last synced text" snapshot and a freshly
// materialized "new text" (which, for an embedded virtual document, is
// never the same string as any live buffer's own storage at all) -- plain
// strings with no ITextStorage wrapper, and building a real
// RopeStorage/PieceTableStorage just to convert one range would be a
// needless tree-build allocation on every sync. startByte and endByte must
// satisfy startByte <= endByte <= content.size().
[[nodiscard]] Range ByteRangeToLspRange(std::string_view content, std::size_t startByte, std::size_t endByte);

// PositionToByte's plain-string sibling, and ByteRangeToLspRange's exact
// inverse -- for a caller holding a file's text but no ITextStorage over it.
// rename-review follow-up: the review resolves a server's edit positions
// against text it read itself (live buffer first, disk otherwise), precisely
// so that reviewing a rename doesn't have to open every file it touches the
// way applying one does. Same bounding as PositionToByte: a line past the
// end clamps to content.size(), and an out-of-range character clamps to that
// line's own end.
[[nodiscard]] std::size_t PositionToByte(std::string_view content, Position position);

// UTF-16 code-unit length of content[startByte, endByte) --
// contentChanges[0].rangeLength, deprecated by the LSP spec but still
// required by some servers. Same content contract as ByteRangeToLspRange.
[[nodiscard]] std::size_t Utf16LengthOfByteRange(std::string_view content, std::size_t startByte, std::size_t endByte);

} // namespace ned::editor::lsp

#endif // NED_EDITOR_LSP_POSITION_H
