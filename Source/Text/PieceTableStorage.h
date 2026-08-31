//
// ITextStorage backed by a PieceTable -- the storage a huge (multi-GB)
// Buffer uses. See PieceTable.h for the actual span-tree/mmap design; this
// is a thin delegating wrapper, the PieceTable-flavored sibling of
// RopeStorage.h.
//

#ifndef NED_TEXT_PIECETABLESTORAGE_H
#define NED_TEXT_PIECETABLESTORAGE_H

#include "ITextStorage.h"
#include "PieceTable.h"

namespace ned::text {

// A thin delegating wrapper -- every method forwards straight to the
// wrapped PieceTable with no logic of its own, mirroring RopeStorage's
// exact shape. IsHuge() is the one substantive difference from RopeStorage:
// true here, so a caller with a size-sensitive operation (full-document
// materialize, LSP sync, ...) can check before paying a cost that's fine
// for an ordinary buffer but not for one of these.
class PieceTableStorage : public ITextStorage {
  public:
    PieceTableStorage() = default;
    explicit PieceTableStorage(PieceTable table);

    // Non-interface accessor for Buffer.cpp's own internal use, mirroring
    // RopeStorage::Value() -- currently unused (Buffer.cpp's huge-file path
    // only needs the ITextStorage surface, plus PieceTable::ForEachChunk
    // for streaming save, called directly against a PieceTableStorage&
    // where that's already known statically rather than through this
    // accessor). Kept for symmetry with RopeStorage and because
    // SaveToFile's streaming path will want it once wired -- see the
    // huge-file-editing plan's step 5.
    [[nodiscard]] const PieceTable& Value() const;

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
    PieceTable table_;
};

} // namespace ned::text

#endif // NED_TEXT_PIECETABLESTORAGE_H
