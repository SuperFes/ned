//
// ITextStorage backed by an ordinary in-memory Rope -- the storage every
// normal (non-huge) Buffer uses, behavior-identical to Buffer holding a
// bare Rope directly the way it did before ITextStorage existed.
//

#ifndef NED_TEXT_ROPESTORAGE_H
#define NED_TEXT_ROPESTORAGE_H

#include "ITextStorage.h"
#include "Rope.h"

namespace ned::text {

// A thin delegating wrapper -- every method forwards straight to the
// wrapped Rope with no logic of its own, so this changes nothing about how
// a normal buffer behaves or performs. Rope() itself (below the interface)
// is exactly as untouched by the huge-file-editing work as ROADMAP.md's own
// plan requires.
class RopeStorage : public ITextStorage {
  public:
    RopeStorage() = default;
    explicit RopeStorage(Rope rope);

    // Non-interface accessor for Buffer.cpp's own internal use (relocation
    // math, SavedSnapshot_ comparisons, etc. that already work in terms of
    // a real Rope value) and for the small set of external Rope-typed call
    // sites this refactor left alone -- see the plan's own notes on which
    // ones those are.
    [[nodiscard]] const Rope& Value() const;

    [[nodiscard]] std::unique_ptr<ITextStorage> Clone() const override;
    [[nodiscard]] bool                          IsHuge() const override;

    [[nodiscard]] bool        Empty() const override;
    [[nodiscard]] std::size_t ByteLength() const override;
    [[nodiscard]] std::size_t CodepointLength() const override;
    [[nodiscard]] std::size_t LineCount() const override;

    [[nodiscard]] std::unique_ptr<ITextStorage> Inserted(std::size_t byteOffset, std::string_view text) const override;
    [[nodiscard]] std::unique_ptr<ITextStorage> Erased(std::size_t byteOffset, std::size_t byteLength) const override;

    [[nodiscard]] std::string ToString() const override;
    [[nodiscard]] std::string Substring(std::size_t byteOffset, std::size_t byteLength) const override;

    [[nodiscard]] std::size_t ByteOffsetToLine(std::size_t byteOffset) const override;
    [[nodiscard]] std::size_t LineToByteOffset(std::size_t line) const override;

    [[nodiscard]] std::size_t ByteOffsetToCodepointOffset(std::size_t byteOffset) const override;
    [[nodiscard]] std::size_t CodepointOffsetToByteOffset(std::size_t codepointOffset) const override;

    [[nodiscard]] DecodedCodepoint CodepointAt(std::size_t byteOffset) const override;
    [[nodiscard]] std::size_t      PreviousCodepointBoundary(std::size_t byteOffset) const override;
    [[nodiscard]] std::size_t      NextCodepointBoundary(std::size_t byteOffset) const override;

    void ForEachChunk(const std::function<void(std::string_view)>& sink) const override;

  private:
    Rope rope_;
};

} // namespace ned::text

#endif // NED_TEXT_ROPESTORAGE_H
