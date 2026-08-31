//
// Storage-agnostic interface Buffer mutates/queries through instead of a
// bare Rope, so a huge (piece-table-backed) buffer can sit behind exactly
// the same Buffer public API as a normal one.
//

#ifndef NED_TEXT_ITEXTSTORAGE_H
#define NED_TEXT_ITEXTSTORAGE_H

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace ned::text {

// Mirrors Rope's own public surface exactly (see Rope.h) -- RopeStorage and
// PieceTableStorage are both thin delegating wrappers around their
// respective concrete type, so neither storage engine's own tree/balancing
// logic changes at all; this interface only exists so Buffer -- and every
// caller of Buffer::Content() -- can hold one reference type regardless of
// which is active underneath. Confirmed against the real call surface
// before adding this: every one of Buffer.cpp's own ~86 internal uses and
// every external caller of Content() (Grapheme.h, LspPosition.h,
// CodeFold.h, BufferView.cpp, ...) only ever calls methods in this exact
// set, so none of their logic needs to change -- only the declared type of
// wherever they bind Content()'s result.
//
// Inserted/Erased return a new storage instance by pointer (not by value,
// unlike Rope's own Inserted/Erased) since a virtual method can't return a
// covariant value type -- Buffer.cpp is the only caller of these two (its
// own InsertAtImpl/DeleteRange etc.), never exposed to anything above
// Buffer, so the shape difference from Rope's own API is invisible outside
// Buffer.cpp.
class ITextStorage {
  public:
    virtual ~ITextStorage() = default;

    // A fresh, independent copy -- the polymorphic counterpart to Rope's own
    // cheap-by-value-copy (structural sharing means this is O(1) for either
    // concrete type, just one small heap allocation for the new wrapper
    // object). What Buffer.cpp uses everywhere it used to do a plain
    // `SavedSnapshot_ = Rope_;`-style value copy.
    [[nodiscard]] virtual std::unique_ptr<ITextStorage> Clone() const = 0;

    // True for a huge (piece-table-backed) buffer -- false for the ordinary
    // Rope-backed case. Buffer.cpp and a small number of external callers
    // (LSP sync, persistent undo) use this to skip an operation that would
    // otherwise force a full-document materialize (ToString()/Text()) on a
    // buffer that's deliberately never fully resident -- see the
    // huge-file-editing plan for the specific gated call sites.
    [[nodiscard]] virtual bool IsHuge() const = 0;

    [[nodiscard]] virtual bool        Empty() const           = 0;
    [[nodiscard]] virtual std::size_t ByteLength() const       = 0;
    [[nodiscard]] virtual std::size_t CodepointLength() const  = 0;
    [[nodiscard]] virtual std::size_t LineCount() const        = 0; // newline count + 1

    [[nodiscard]] virtual std::unique_ptr<ITextStorage> Inserted(std::size_t byteOffset, std::string_view text) const  = 0;
    [[nodiscard]] virtual std::unique_ptr<ITextStorage> Erased(std::size_t byteOffset, std::size_t byteLength) const   = 0;

    [[nodiscard]] virtual std::string ToString() const                                                       = 0;
    [[nodiscard]] virtual std::string Substring(std::size_t byteOffset, std::size_t byteLength) const         = 0;

    // 0-indexed line number containing byteOffset / byte offset where a
    // given 0-indexed line starts.
    [[nodiscard]] virtual std::size_t ByteOffsetToLine(std::size_t byteOffset) const = 0;
    [[nodiscard]] virtual std::size_t LineToByteOffset(std::size_t line) const       = 0;

    [[nodiscard]] virtual std::size_t ByteOffsetToCodepointOffset(std::size_t byteOffset) const           = 0;
    [[nodiscard]] virtual std::size_t CodepointOffsetToByteOffset(std::size_t codepointOffset) const      = 0;

    struct DecodedCodepoint {
        char32_t    codepoint;
        std::size_t byteLength;
    };

    // Decodes the codepoint starting at byteOffset. Malformed/truncated
    // UTF-8 decodes as U+FFFD with byteLength >= 1, so callers always make
    // forward progress -- same contract as Rope::CodepointAt/
    // PieceTable::CodepointAt.
    [[nodiscard]] virtual DecodedCodepoint CodepointAt(std::size_t byteOffset) const = 0;

    [[nodiscard]] virtual std::size_t PreviousCodepointBoundary(std::size_t byteOffset) const = 0;
    [[nodiscard]] virtual std::size_t NextCodepointBoundary(std::size_t byteOffset) const      = 0;

    // huge-file-editing follow-up (streaming save): invokes sink once per
    // internal chunk, left to right, covering the whole content -- never
    // materializes the whole document into one string. RopeStorage's
    // implementation is the trivial single-chunk case (sink(ToString())) --
    // an ordinary buffer is never huge, so this is exactly what already
    // happens today, just funneled through a callback instead of a return
    // value; zero behavior change for a normal buffer. PieceTableStorage
    // delegates to PieceTable::ForEachChunk (Text/PieceTable.h), whose
    // chunks are plain byte spans, NOT line- or otherwise semantically
    // aligned -- a caller processing a logical unit (a line, a trimmed
    // trailing-whitespace run) spanning a chunk boundary must carry its own
    // state across sink calls, the same way Buffer::SaveToFile's streaming
    // path does.
    virtual void ForEachChunk(const std::function<void(std::string_view)>& sink) const = 0;
};

} // namespace ned::text

#endif // NED_TEXT_ITEXTSTORAGE_H
