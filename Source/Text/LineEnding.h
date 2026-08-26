//
// Line-ending detection/normalization, shared by Buffer::FromFile/SaveToFile
// and UI/AsyncFileLoader. Ned's internal representation (Rope, UndoTree,
// Grapheme column math) is always LF-only -- a loaded file's line endings
// are detected once, then normalized away before the content ever reaches a
// Rope, the same way Buffer::FromFile already strips a leading UTF-8 BOM
// before construction. Save time re-expands LF back to the buffer's chosen
// ending on a disk-bound copy only, mirroring Editor/FinalNewline.h and
// Editor/TrimOnSave.h's own disk-only-transform precedent -- Rope_ itself
// never holds a '\r'.
//

#ifndef NED_TEXT_LINEENDING_H
#define NED_TEXT_LINEENDING_H

#include <string>
#include <string_view>

namespace ned::text {

enum class LineEnding {
    LF,   // '\n' -- Unix/macOS/Linux
    CRLF, // '\r\n' -- Windows
    CR,   // '\r' -- classic (pre-OS X) Mac
};

// Majority vote over every line terminator found in content: CRLF pairs,
// then remaining lone '\n's, then remaining lone '\r's. A tie or a file with
// no line terminators at all (including an empty file) reports LF -- the
// harmless default every freshly-created buffer already implicitly uses.
[[nodiscard]] LineEnding DetectLineEnding(std::string_view content);

// Rewrites every CRLF pair and every remaining lone '\r' to '\n'. Content
// already LF-only passes through unchanged (and unallocated, via the
// string_view fast path some callers may prefer -- see the two-arg overload
// below for in-place use). Idempotent.
[[nodiscard]] std::string NormalizeToLf(std::string_view content);

// True if content contains any '\r' at all -- a cheap pre-check so a caller
// (Buffer::FromFile) can skip NormalizeToLf's copy entirely for the common
// already-LF case.
[[nodiscard]] bool HasCarriageReturn(std::string_view content);

// Expands an LF-only string to the given ending. LF is a no-op passthrough;
// CRLF/CR both replace every '\n' with the two/one-byte target sequence.
// lfContent is assumed already LF-normalized (as Rope_::ToString() always
// is) -- a stray '\r' in it is passed through verbatim rather than
// double-encoded.
[[nodiscard]] std::string ApplyLineEnding(std::string_view lfContent, LineEnding ending);

// "LF"/"CRLF"/"CR" -- the exact tokens the mode line and
// ned/set-line-ending-policy's Janet surface both use.
[[nodiscard]] const char* LineEndingName(LineEnding ending);

} // namespace ned::text

#endif // NED_TEXT_LINEENDING_H
