#include "RopeStorage.h"

#include <utility>

namespace ned::text {

RopeStorage::RopeStorage(Rope rope) : rope_(std::move(rope)) {}

const Rope& RopeStorage::Value() const {
    return rope_;
}

std::unique_ptr<ITextStorage> RopeStorage::Clone() const {
    return std::make_unique<RopeStorage>(rope_);
}

bool RopeStorage::IsHuge() const {
    return false;
}

bool RopeStorage::Empty() const {
    return rope_.Empty();
}

std::size_t RopeStorage::ByteLength() const {
    return rope_.ByteLength();
}

std::size_t RopeStorage::CodepointLength() const {
    return rope_.CodepointLength();
}

std::size_t RopeStorage::LineCount() const {
    return rope_.LineCount();
}

std::unique_ptr<ITextStorage> RopeStorage::Inserted(std::size_t byteOffset, std::string_view text) const {
    return std::make_unique<RopeStorage>(rope_.Inserted(byteOffset, text));
}

std::unique_ptr<ITextStorage> RopeStorage::Erased(std::size_t byteOffset, std::size_t byteLength) const {
    return std::make_unique<RopeStorage>(rope_.Erased(byteOffset, byteLength));
}

std::string RopeStorage::ToString() const {
    return rope_.ToString();
}

std::string RopeStorage::Substring(std::size_t byteOffset, std::size_t byteLength) const {
    return rope_.Substring(byteOffset, byteLength);
}

std::size_t RopeStorage::ByteOffsetToLine(std::size_t byteOffset) const {
    return rope_.ByteOffsetToLine(byteOffset);
}

std::size_t RopeStorage::LineToByteOffset(std::size_t line) const {
    return rope_.LineToByteOffset(line);
}

std::size_t RopeStorage::ByteOffsetToCodepointOffset(std::size_t byteOffset) const {
    return rope_.ByteOffsetToCodepointOffset(byteOffset);
}

std::size_t RopeStorage::CodepointOffsetToByteOffset(std::size_t codepointOffset) const {
    return rope_.CodepointOffsetToByteOffset(codepointOffset);
}

ITextStorage::DecodedCodepoint RopeStorage::CodepointAt(std::size_t byteOffset) const {
    const Rope::DecodedCodepoint decoded = rope_.CodepointAt(byteOffset);
    return {decoded.codepoint, decoded.byteLength};
}

std::size_t RopeStorage::PreviousCodepointBoundary(std::size_t byteOffset) const {
    return rope_.PreviousCodepointBoundary(byteOffset);
}

std::size_t RopeStorage::NextCodepointBoundary(std::size_t byteOffset) const {
    return rope_.NextCodepointBoundary(byteOffset);
}

void RopeStorage::ForEachChunk(const std::function<void(std::string_view)>& sink) const {
    sink(rope_.ToString());
}

} // namespace ned::text
