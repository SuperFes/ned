//
// Persistent (immutable) rope over UTF-8 byte sequences.
//

#ifndef NED_TEXT_ROPE_H
#define NED_TEXT_ROPE_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ned::text {

// Editing operations (Inserted/Erased) return a new Rope rather than mutating
// this one; unchanged subtrees are shared via shared_ptr with the original, so
// snapshots (e.g. one per undo-history node) are O(log n), not full copies.
//
// Offsets are byte offsets into the UTF-8 encoding and must land on codepoint
// boundaries — callers working in grapheme-cluster terms (see Grapheme.h) are
// responsible for that; this class only guarantees not to split a codepoint.
class Rope {
  public:
    Rope();
    explicit Rope(std::string_view text);

    [[nodiscard]] bool Empty() const;
    [[nodiscard]] std::size_t ByteLength() const;
    [[nodiscard]] std::size_t CodepointLength() const;
    [[nodiscard]] std::size_t LineCount() const; // newline count + 1

    [[nodiscard]] Rope Inserted(std::size_t byteOffset, std::string_view text) const;
    [[nodiscard]] Rope Erased(std::size_t byteOffset, std::size_t byteLength) const;

    [[nodiscard]] std::string ToString() const;
    [[nodiscard]] std::string Substring(std::size_t byteOffset, std::size_t byteLength) const;

    // 0-indexed line number containing byteOffset / byte offset where a given
    // 0-indexed line starts.
    [[nodiscard]] std::size_t ByteOffsetToLine(std::size_t byteOffset) const;
    [[nodiscard]] std::size_t LineToByteOffset(std::size_t line) const;

    [[nodiscard]] std::size_t ByteOffsetToCodepointOffset(std::size_t byteOffset) const;
    [[nodiscard]] std::size_t CodepointOffsetToByteOffset(std::size_t codepointOffset) const;

    struct DecodedCodepoint {
        char32_t    codepoint;
        std::size_t byteLength;
    };

    // Decodes the codepoint starting at byteOffset. Malformed/truncated UTF-8
    // decodes as U+FFFD with byteLength >= 1, so callers always make forward
    // progress.
    [[nodiscard]] DecodedCodepoint CodepointAt(std::size_t byteOffset) const;

    [[nodiscard]] std::size_t PreviousCodepointBoundary(std::size_t byteOffset) const;
    [[nodiscard]] std::size_t NextCodepointBoundary(std::size_t byteOffset) const;

  private:
    struct Node;

    explicit Rope(std::shared_ptr<const Node> root);

    std::shared_ptr<const Node> root_;

    static std::shared_ptr<const Node> MakeLeaf(std::string_view text);
    static std::shared_ptr<const Node> MakeInternal(std::shared_ptr<const Node> left, std::shared_ptr<const Node> right);
    static std::shared_ptr<const Node> Concat(std::shared_ptr<const Node> left, std::shared_ptr<const Node> right);
    static std::shared_ptr<const Node> RotateLeft(const std::shared_ptr<const Node>& node);
    static std::shared_ptr<const Node> RotateRight(const std::shared_ptr<const Node>& node);
    static std::shared_ptr<const Node> Rebalance(std::shared_ptr<const Node> node);
    static std::shared_ptr<const Node> BuildBalanced(std::string_view text);
    static std::shared_ptr<const Node> BuildBalanced(std::vector<std::shared_ptr<const Node>> leaves);
    static std::pair<std::shared_ptr<const Node>, std::shared_ptr<const Node>> Split(const std::shared_ptr<const Node>& node, std::size_t byteOffset);
    static void AppendToString(const std::shared_ptr<const Node>& node, std::string& out);

    static std::size_t CountNewlinesBefore(const std::shared_ptr<const Node>& node, std::size_t byteOffset);
    static std::size_t FindLineStart(const std::shared_ptr<const Node>& node, std::size_t line);
    static std::size_t CountCodepointsBefore(const std::shared_ptr<const Node>& node, std::size_t byteOffset);
    static std::size_t FindCodepointStart(const std::shared_ptr<const Node>& node, std::size_t codepointOffset);
    static std::uint8_t ByteAt(const std::shared_ptr<const Node>& node, std::size_t byteOffset);
};

} // namespace ned::text

#endif // NED_TEXT_ROPE_H
