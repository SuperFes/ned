//
// embedded-language-documents follow-up: turns a Mode's raw
// Mode::embeddedRegions output (Editor/Injection.h's tree-sitter-injection
// engine, as data rather than highlight spans) into synced-to-a-real-LSP-
// server virtual documents -- one per distinct embedded language, merging
// every same-language region (e.g. two separate <script> blocks) into a
// single document, matching how a real language server treats multiple
// <script> blocks as sharing one JS global scope.
//
// Width-preserving padding (see BuildEmbeddedDocuments' own doc comment) is
// what lets Editor/Lsp/LspManager.h sync the resulting documentText to a real
// server and use LspPosition.h's ordinary BytePositionToLsp/LspPositionToByte
// completely unchanged, against either the padded text or the real host
// buffer's own Rope -- both agree on every line boundary and every
// codepoint's UTF-16 width, so no offset-remapping layer exists anywhere in
// this codebase for this feature. See ROADMAP.md's "Embedded-language
// documents" entry for the fuller design writeup.
//

#ifndef NED_EDITOR_EMBEDDEDDOCUMENTS_H
#define NED_EDITOR_EMBEDDEDDOCUMENTS_H

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Mode.h"

namespace ned::editor {

// One embedded language's synthesized virtual document. documentText is the
// exact same byte length as the host buffer text it was built from, and
// agrees with it on every line boundary and every codepoint's UTF-16 width
// (see BuildEmbeddedDocuments) -- everything outside ownedRanges is replaced
// with Unicode-whitespace filler, never the real host content. ownedRanges
// (sorted, non-overlapping, host-buffer byte coordinates) is what
// EmbeddedLanguageAtByteOffset and LspManager's diagnostics filtering both
// consult to tell "real content for this language" from "padding."
struct EmbeddedDocument {
    std::string                                      language; // canonical, e.g. "javascript"
    std::string                                      documentText;
    std::vector<std::pair<std::size_t, std::size_t>> ownedRanges;
};

// Empty when mode.embeddedRegions is unset (every bundled mode but
// html-mode) or reports no regions -- the common, cheap-to-check case. When
// non-empty, one EmbeddedDocument per distinct language found, each built by
// walking bufferText codepoint-by-codepoint (via text::Rope::CodepointAt, the
// same primitive LspPosition.cpp's own position math already uses) and
// replacing every codepoint OUTSIDE that language's own merged ranges with a
// same-byte-length, same-UTF-16-width Unicode whitespace filler ('\n' is
// always copied verbatim, preserving line structure regardless of
// ownership). A 4-byte original codepoint (necessarily > U+FFFF, i.e. 2
// UTF-16 units) is replaced with two 2-byte NBSPs rather than inventing a
// single astral filler -- there is no codepoint above the Basic Multilingual
// Plane that Unicode itself classifies as whitespace, so two real, always-
// safe-to-parse NBSPs (4 bytes, 2 UTF-16 units total) reproduce both the byte
// length and the UTF-16 width exactly, with zero risk of confusing whatever
// language ends up parsing the padded region.
[[nodiscard]] std::vector<EmbeddedDocument> BuildEmbeddedDocuments(const Mode& mode, std::string_view bufferText);

// nullopt if byteOffset isn't inside any document's ownedRanges (the ordinary
// single-language case, or point sitting in the host-language chrome around
// an embedded region) -- otherwise the owning document's language. Shared by
// LSP-request routing (BufferView/Commands.cpp resolve which server a
// hover/completion/definition/rename at point should hit) and the mode-line
// "language at point" display.
[[nodiscard]] std::optional<std::string> EmbeddedLanguageAtByteOffset(const std::vector<EmbeddedDocument>& documents,
                                                                      std::size_t                          byteOffset);

// Uncached convenience wrapper (BuildEmbeddedDocuments + EmbeddedLanguageAtByteOffset
// in one call) for a call site with no BufferView-owned per-Paint() cache to
// reuse -- Commands.cpp's lsp-hover, whose CommandContext carries no
// BufferView&. Returns "" (meaning "use the primary/host server," matching
// LspManager::ResolveSyncState's own empty-serverKey convention) when point
// isn't inside an embedded region, or mode has none configured. Cheap in
// practice even though it re-derives BuildEmbeddedDocuments from scratch:
// mode.embeddedRegions' closure (HtmlMode()) shares the same
// IncrementalParseCache its own mode.highlight closure already populated for
// this buffer's current text, so this hits an already-parsed tree rather
// than reparsing.
[[nodiscard]] std::string ResolveLspServerKey(const Mode& mode, std::string_view bufferText, std::size_t byteOffset);

} // namespace ned::editor

#endif // NED_EDITOR_EMBEDDEDDOCUMENTS_H
