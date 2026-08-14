#include "Rope.h"

#include <algorithm>

namespace ned::text {

namespace {
constexpr std::size_t kChunkSize = 512;

bool IsContinuationByte(char c) {
    return (static_cast<unsigned char>(c) & 0xC0) == 0x80;
}

std::size_t CountCodepoints(std::string_view text) {
    std::size_t count = 0;
    for (unsigned char c : text) {
        if (!IsContinuationByte(static_cast<char>(c))) {
            ++count;
        }
    }
    return count;
}

std::size_t CountNewlines(std::string_view text) {
    return static_cast<std::size_t>(std::count(text.begin(), text.end(), '\n'));
}
} // namespace

struct Rope::Node {
    std::size_t byteLength      = 0;
    std::size_t codepointLength = 0;
    std::size_t lineCount       = 0;
    std::size_t depth           = 0;

    std::string                 text;  // leaf only
    std::shared_ptr<const Node> left;  // internal only
    std::shared_ptr<const Node> right; // internal only

    [[nodiscard]] bool IsLeaf() const {
        return left == nullptr;
    }
};

Rope::Rope() : root_(nullptr) {}

Rope::Rope(std::string_view text) : root_(BuildBalanced(text)) {}

Rope::Rope(std::shared_ptr<const Node> root) : root_(std::move(root)) {}

bool Rope::Empty() const {
    return root_ == nullptr;
}

std::size_t Rope::ByteLength() const {
    return root_ ? root_->byteLength : 0;
}

std::size_t Rope::CodepointLength() const {
    return root_ ? root_->codepointLength : 0;
}

std::size_t Rope::LineCount() const {
    return (root_ ? root_->lineCount : 0) + 1;
}

std::shared_ptr<const Rope::Node> Rope::MakeLeaf(std::string_view text) {
    if (text.empty()) {
        return nullptr;
    }

    auto node             = std::make_shared<Node>();
    node->text            = std::string(text);
    node->byteLength      = text.size();
    node->codepointLength = CountCodepoints(text);
    node->lineCount       = CountNewlines(text);
    node->depth           = 0;

    return node;
}

std::shared_ptr<const Rope::Node> Rope::MakeInternal(std::shared_ptr<const Node> left, std::shared_ptr<const Node> right) {
    if (!left) {
        return right;
    }
    if (!right) {
        return left;
    }

    auto node             = std::make_shared<Node>();
    node->byteLength      = left->byteLength + right->byteLength;
    node->codepointLength = left->codepointLength + right->codepointLength;
    node->lineCount       = left->lineCount + right->lineCount;
    node->depth           = 1 + std::max(left->depth, right->depth);
    node->left            = std::move(left);
    node->right           = std::move(right);

    return node;
}

std::shared_ptr<const Rope::Node> Rope::RotateLeft(const std::shared_ptr<const Node>& node) {
    auto pivot = node->right;
    return MakeInternal(MakeInternal(node->left, pivot->left), pivot->right);
}

std::shared_ptr<const Rope::Node> Rope::RotateRight(const std::shared_ptr<const Node>& node) {
    auto pivot = node->left;
    return MakeInternal(pivot->left, MakeInternal(pivot->right, node->right));
}

// AVL-style rebalance: node's two children are each already internally
// balanced (Concat's invariant, below), so the top can be off by at most one
// extra level -- a single rotation (or a double rotation, for the
// left-right/right-left case) always suffices to fix it.
std::shared_ptr<const Rope::Node> Rope::Rebalance(std::shared_ptr<const Node> node) {
    const std::size_t leftDepth  = node->left->depth;
    const std::size_t rightDepth = node->right->depth;

    if (leftDepth > rightDepth + 1) {
        auto left = node->left;
        if (left->right->depth > left->left->depth) {
            node = MakeInternal(RotateLeft(left), node->right);
        }
        return RotateRight(node);
    }
    if (rightDepth > leftDepth + 1) {
        auto right = node->right;
        if (right->left->depth > right->right->depth) {
            node = MakeInternal(node->left, RotateRight(right));
        }
        return RotateLeft(node);
    }
    return node;
}

// Joins two already-balanced subtrees in O(|depth(left) - depth(right)|)
// time by descending into whichever side is taller and rebalancing on the
// way back up, rather than a plain MakeInternal (which would grow the
// tree's height by ~1 per edit) plus a periodic full-tree flatten to fix it.
// That older approach made a long editing session cost proportional to
// document size rather than edit count: any node deep enough to trip a
// depth ceiling forced an O(document size) rebuild, and normal editing hits
// that ceiling repeatedly regardless of where in the document you edit.
std::shared_ptr<const Rope::Node> Rope::Concat(std::shared_ptr<const Node> left, std::shared_ptr<const Node> right) {
    if (!left) {
        return right;
    }
    if (!right) {
        return left;
    }

    if (left->depth > right->depth + 1) {
        return Rebalance(MakeInternal(left->left, Concat(left->right, right)));
    }
    if (right->depth > left->depth + 1) {
        return Rebalance(MakeInternal(Concat(left, right->left), right->right));
    }
    return MakeInternal(std::move(left), std::move(right));
}

std::shared_ptr<const Rope::Node> Rope::BuildBalanced(std::vector<std::shared_ptr<const Node>> leaves) {
    if (leaves.empty()) {
        return nullptr;
    }

    while (leaves.size() > 1) {
        std::vector<std::shared_ptr<const Node>> next;
        next.reserve((leaves.size() + 1) / 2);

        for (std::size_t i = 0; i < leaves.size(); i += 2) {
            if (i + 1 < leaves.size()) {
                next.push_back(MakeInternal(leaves[i], leaves[i + 1]));
            } else {
                next.push_back(leaves[i]);
            }
        }

        leaves = std::move(next);
    }

    return leaves.front();
}

std::shared_ptr<const Rope::Node> Rope::BuildBalanced(std::string_view text) {
    if (text.empty()) {
        return nullptr;
    }

    std::vector<std::shared_ptr<const Node>> leaves;
    std::size_t                              offset = 0;

    while (offset < text.size()) {
        std::size_t chunkEnd = std::min(offset + kChunkSize, text.size());

        while (chunkEnd < text.size() && IsContinuationByte(text[chunkEnd])) {
            --chunkEnd;
        }

        leaves.push_back(MakeLeaf(text.substr(offset, chunkEnd - offset)));
        offset = chunkEnd;
    }

    return BuildBalanced(std::move(leaves));
}

std::pair<std::shared_ptr<const Rope::Node>, std::shared_ptr<const Rope::Node>> Rope::Split(const std::shared_ptr<const Node>& node, std::size_t offset) {
    if (!node) {
        return {nullptr, nullptr};
    }
    if (offset == 0) {
        return {nullptr, node};
    }
    if (offset >= node->byteLength) {
        return {node, nullptr};
    }
    if (node->IsLeaf()) {
        std::string_view text(node->text);
        return {MakeLeaf(text.substr(0, offset)), MakeLeaf(text.substr(offset))};
    }

    const std::size_t leftLen = node->left->byteLength;

    if (offset < leftLen) {
        auto [ll, lr] = Split(node->left, offset);
        return {ll, Concat(lr, node->right)};
    }
    if (offset > leftLen) {
        auto [rl, rr] = Split(node->right, offset - leftLen);
        return {Concat(node->left, rl), rr};
    }
    return {node->left, node->right};
}

Rope Rope::Inserted(std::size_t byteOffset, std::string_view text) const {
    if (text.empty()) {
        return *this;
    }

    byteOffset = std::min(byteOffset, ByteLength());

    auto [left, right] = Split(root_, byteOffset);
    auto middle         = BuildBalanced(text);

    return Rope(Concat(Concat(left, middle), right));
}

Rope Rope::Erased(std::size_t byteOffset, std::size_t byteLength) const {
    byteOffset = std::min(byteOffset, ByteLength());
    byteLength = std::min(byteLength, ByteLength() - byteOffset);

    if (byteLength == 0) {
        return *this;
    }

    auto [left, rest]    = Split(root_, byteOffset);
    auto [middle, right] = Split(rest, byteLength);
    (void)middle;

    return Rope(Concat(left, right));
}

void Rope::AppendToString(const std::shared_ptr<const Node>& node, std::string& out) {
    if (!node) {
        return;
    }
    if (node->IsLeaf()) {
        out += node->text;
        return;
    }
    AppendToString(node->left, out);
    AppendToString(node->right, out);
}

std::string Rope::ToString() const {
    std::string out;
    out.reserve(ByteLength());
    AppendToString(root_, out);
    return out;
}

std::string Rope::Substring(std::size_t byteOffset, std::size_t byteLength) const {
    byteOffset = std::min(byteOffset, ByteLength());
    byteLength = std::min(byteLength, ByteLength() - byteOffset);

    auto [_, rest]        = Split(root_, byteOffset);
    auto [middle, right]  = Split(rest, byteLength);
    (void)right;

    std::string out;
    out.reserve(byteLength);
    AppendToString(middle, out);
    return out;
}

std::size_t Rope::CountNewlinesBefore(const std::shared_ptr<const Node>& node, std::size_t offset) {
    if (!node || offset == 0) {
        return 0;
    }
    if (node->IsLeaf()) {
        const std::size_t limit = std::min(offset, node->byteLength);
        return static_cast<std::size_t>(std::count(node->text.begin(), node->text.begin() + static_cast<std::ptrdiff_t>(limit), '\n'));
    }

    const std::size_t leftLen = node->left->byteLength;
    if (offset <= leftLen) {
        return CountNewlinesBefore(node->left, offset);
    }
    return node->left->lineCount + CountNewlinesBefore(node->right, offset - leftLen);
}

std::size_t Rope::ByteOffsetToLine(std::size_t byteOffset) const {
    return CountNewlinesBefore(root_, std::min(byteOffset, ByteLength()));
}

std::size_t Rope::FindLineStart(const std::shared_ptr<const Node>& node, std::size_t line) {
    if (!node || line == 0) {
        return 0;
    }
    if (node->IsLeaf()) {
        std::size_t seen = 0;
        for (std::size_t i = 0; i < node->text.size(); ++i) {
            if (node->text[i] == '\n') {
                ++seen;
                if (seen == line) {
                    return i + 1;
                }
            }
        }
        return node->byteLength;
    }

    if (node->left->lineCount >= line) {
        return FindLineStart(node->left, line);
    }
    return node->left->byteLength + FindLineStart(node->right, line - node->left->lineCount);
}

std::size_t Rope::LineToByteOffset(std::size_t line) const {
    if (line == 0 || !root_) {
        return 0;
    }
    if (line > root_->lineCount) {
        return root_->byteLength;
    }
    return FindLineStart(root_, line);
}

std::size_t Rope::CountCodepointsBefore(const std::shared_ptr<const Node>& node, std::size_t offset) {
    if (!node || offset == 0) {
        return 0;
    }
    if (node->IsLeaf()) {
        const std::size_t limit = std::min(offset, node->byteLength);
        std::size_t       count = 0;
        for (std::size_t i = 0; i < limit; ++i) {
            if (!IsContinuationByte(node->text[i])) {
                ++count;
            }
        }
        return count;
    }

    const std::size_t leftLen = node->left->byteLength;
    if (offset <= leftLen) {
        return CountCodepointsBefore(node->left, offset);
    }
    return node->left->codepointLength + CountCodepointsBefore(node->right, offset - leftLen);
}

std::size_t Rope::ByteOffsetToCodepointOffset(std::size_t byteOffset) const {
    return CountCodepointsBefore(root_, std::min(byteOffset, ByteLength()));
}

std::size_t Rope::FindCodepointStart(const std::shared_ptr<const Node>& node, std::size_t codepointOffset) {
    if (!node || codepointOffset == 0) {
        return 0;
    }
    if (node->IsLeaf()) {
        std::size_t seen = 0;
        for (std::size_t i = 0; i < node->text.size(); ++i) {
            if (!IsContinuationByte(node->text[i])) {
                if (seen == codepointOffset) {
                    return i;
                }
                ++seen;
            }
        }
        return node->byteLength;
    }

    if (node->left->codepointLength >= codepointOffset) {
        return FindCodepointStart(node->left, codepointOffset);
    }
    return node->left->byteLength + FindCodepointStart(node->right, codepointOffset - node->left->codepointLength);
}

std::size_t Rope::CodepointOffsetToByteOffset(std::size_t codepointOffset) const {
    if (codepointOffset == 0 || !root_) {
        return 0;
    }
    if (codepointOffset >= root_->codepointLength) {
        return root_->byteLength;
    }
    return FindCodepointStart(root_, codepointOffset);
}

std::uint8_t Rope::ByteAt(const std::shared_ptr<const Node>& node, std::size_t offset) {
    if (node->IsLeaf()) {
        return static_cast<std::uint8_t>(node->text[offset]);
    }

    const std::size_t leftLen = node->left->byteLength;
    if (offset < leftLen) {
        return ByteAt(node->left, offset);
    }
    return ByteAt(node->right, offset - leftLen);
}

Rope::DecodedCodepoint Rope::CodepointAt(std::size_t byteOffset) const {
    const std::size_t total = ByteLength();
    if (byteOffset >= total) {
        return {0xFFFD, 0};
    }

    const std::uint8_t b0 = ByteAt(root_, byteOffset);

    if (b0 < 0x80) {
        return {static_cast<char32_t>(b0), 1};
    }

    std::size_t len;
    char32_t    cp;

    if ((b0 & 0xE0) == 0xC0) {
        len = 2;
        cp  = b0 & 0x1F;
    } else if ((b0 & 0xF0) == 0xE0) {
        len = 3;
        cp  = b0 & 0x0F;
    } else if ((b0 & 0xF8) == 0xF0) {
        len = 4;
        cp  = b0 & 0x07;
    } else {
        return {0xFFFD, 1};
    }

    if (byteOffset + len > total) {
        return {0xFFFD, 1};
    }

    for (std::size_t i = 1; i < len; ++i) {
        const std::uint8_t b = ByteAt(root_, byteOffset + i);
        if (!IsContinuationByte(static_cast<char>(b))) {
            return {0xFFFD, 1};
        }
        cp = (cp << 6) | (b & 0x3F);
    }

    return {cp, len};
}

std::size_t Rope::PreviousCodepointBoundary(std::size_t byteOffset) const {
    if (byteOffset == 0) {
        return 0;
    }

    std::size_t offset = byteOffset - 1;
    std::size_t steps  = 0;

    while (offset > 0 && IsContinuationByte(static_cast<char>(ByteAt(root_, offset))) && steps < 3) {
        --offset;
        ++steps;
    }

    return offset;
}

std::size_t Rope::NextCodepointBoundary(std::size_t byteOffset) const {
    const std::size_t total = ByteLength();
    if (byteOffset >= total) {
        return total;
    }
    return byteOffset + CodepointAt(byteOffset).byteLength;
}

} // namespace ned::text
