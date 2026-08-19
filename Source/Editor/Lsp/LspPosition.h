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

namespace ned::text {
class Rope;
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
[[nodiscard]] LspPosition BytePositionToLsp(const text::Rope& content, std::size_t byteOffset);

// Converts an LSP line/UTF-16-character Position back to a buffer byte
// offset. Bounded by the line's own byte range, so a malformed/out-of-range
// server-reported character can't walk off the end of the line.
[[nodiscard]] std::size_t LspPositionToByte(const text::Rope& content, LspPosition position);

} // namespace ned::editor::lsp

#endif // NED_EDITOR_LSP_LSPPOSITION_H
