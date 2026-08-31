#include "PieceTableStorage.h"

#include <utility>

namespace ned::text {

PieceTableStorage::PieceTableStorage(PieceTable table) : table_(std::move(table)) {}

const PieceTable& PieceTableStorage::Value() const {
    return table_;
}

std::unique_ptr<ITextStorage> PieceTableStorage::Clone() const {
    return std::make_unique<PieceTableStorage>(table_);
}

bool PieceTableStorage::IsHuge() const {
    return true;
}

bool PieceTableStorage::Empty() const {
    return table_.Empty();
}

std::size_t PieceTableStorage::ByteLength() const {
    return table_.ByteLength();
}

std::size_t PieceTableStorage::CodepointLength() const {
    return table_.CodepointLength();
}

std::size_t PieceTableStorage::LineCount() const {
    return table_.LineCount();
}

std::unique_ptr<ITextStorage> PieceTableStorage::Inserted(std::size_t byteOffset, std::string_view text) const {
    return std::make_unique<PieceTableStorage>(table_.Inserted(byteOffset, text));
}

std::unique_ptr<ITextStorage> PieceTableStorage::Erased(std::size_t byteOffset, std::size_t byteLength) const {
    return std::make_unique<PieceTableStorage>(table_.Erased(byteOffset, byteLength));
}

std::string PieceTableStorage::ToString() const {
    return table_.ToString();
}

std::string PieceTableStorage::Substring(std::size_t byteOffset, std::size_t byteLength) const {
    return table_.Substring(byteOffset, byteLength);
}

std::size_t PieceTableStorage::ByteOffsetToLine(std::size_t byteOffset) const {
    return table_.ByteOffsetToLine(byteOffset);
}

std::size_t PieceTableStorage::LineToByteOffset(std::size_t line) const {
    return table_.LineToByteOffset(line);
}

std::size_t PieceTableStorage::ByteOffsetToCodepointOffset(std::size_t byteOffset) const {
    return table_.ByteOffsetToCodepointOffset(byteOffset);
}

std::size_t PieceTableStorage::CodepointOffsetToByteOffset(std::size_t codepointOffset) const {
    return table_.CodepointOffsetToByteOffset(codepointOffset);
}

ITextStorage::DecodedCodepoint PieceTableStorage::CodepointAt(std::size_t byteOffset) const {
    const PieceTable::DecodedCodepoint decoded = table_.CodepointAt(byteOffset);
    return {decoded.codepoint, decoded.byteLength};
}

std::size_t PieceTableStorage::PreviousCodepointBoundary(std::size_t byteOffset) const {
    return table_.PreviousCodepointBoundary(byteOffset);
}

std::size_t PieceTableStorage::NextCodepointBoundary(std::size_t byteOffset) const {
    return table_.NextCodepointBoundary(byteOffset);
}

void PieceTableStorage::ForEachChunk(const std::function<void(std::string_view)>& sink) const {
    table_.ForEachChunk(sink);
}

} // namespace ned::text
