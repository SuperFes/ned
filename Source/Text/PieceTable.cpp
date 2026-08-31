#include "PieceTable.h"

#include <algorithm>

#include "MappedFile.h"

namespace ned::text {

namespace {
// Added text (Inserted) mirrors Rope.cpp's own kChunkSize: these leaves
// hold copied bytes the same way Rope's do, and inserted-text volume is
// naturally small (proportional to what the user has actually typed/pasted
// this session, not file size), so fine granularity here costs nothing
// meaningful.
constexpr std::size_t kAddedChunkSize = 512;

// The original file's span, by contrast, can be many GB, and its leaves
// hold no bytes of their own -- just a {start, length} descriptor -- so
// node *count* is pure overhead with nothing to do with the file's actual
// content residency. 512-byte chunks over a 3 GiB file cost roughly 1.3 GB
// in Node objects alone (measured, not estimated) -- more than the file
// itself is ever resident as mmap pages. 256 KiB keeps that under 3 MB
// while still bounding a single split's rescan cost (see BuildBalancedSpan's
// own comment) to something sub-millisecond.
constexpr std::size_t kOriginalChunkSize = 256 * 1024;

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

struct PieceTable::Node {
    std::size_t byteLength      = 0;
    std::size_t codepointLength = 0;
    std::size_t lineCount       = 0;
    std::size_t depth           = 0;

    SpanSource  source = SpanSource::kAdded; // leaf only
    std::size_t start  = 0;                  // leaf only: offset into whichever storage `source` names

    std::shared_ptr<const Node> left;  // internal only
    std::shared_ptr<const Node> right; // internal only

    [[nodiscard]] bool IsLeaf() const {
        return left == nullptr;
    }
};

PieceTable::PieceTable() : root_(nullptr), backing_{} {}

PieceTable::PieceTable(std::shared_ptr<const Node> root, Backing backing) : root_(std::move(root)), backing_(std::move(backing)) {}

PieceTable PieceTable::FromFile(const std::filesystem::path& path) {
    auto    mappedFile = std::make_shared<MappedFile>(MappedFile::Open(path));
    Backing backing{mappedFile, std::make_shared<std::string>()};

    // The scan below touches every byte once (to seed each leaf's
    // codepoint/newline counts) -- necessary, but a one-time cost, not a
    // steady-state one. Hint sequential for this pass, then release every
    // page it faulted in and switch to a random-access hint for whatever
    // comes after: real interactive editing only ever touches small
    // ranges near the viewport/edit point from here on, so there's no
    // reason a multi-GB open should leave the whole file resident. See
    // MappedFile.h's own comment on this model.
    mappedFile->Advise(AccessPattern::kSequential);
    auto root = BuildBalancedSpan(SpanSource::kOriginal, 0, mappedFile->Size(), kOriginalChunkSize, backing);
    mappedFile->ReleasePages(0, mappedFile->Size());
    mappedFile->Advise(AccessPattern::kRandom);

    return PieceTable(std::move(root), backing);
}

bool PieceTable::Empty() const {
    return root_ == nullptr;
}

std::size_t PieceTable::ByteLength() const {
    return root_ ? root_->byteLength : 0;
}

std::size_t PieceTable::CodepointLength() const {
    return root_ ? root_->codepointLength : 0;
}

std::size_t PieceTable::LineCount() const {
    return (root_ ? root_->lineCount : 0) + 1;
}

std::string_view PieceTable::SpanView(const Node& leaf, const Backing& backing) {
    if (leaf.source == SpanSource::kOriginal) {
        return std::string_view(backing.mappedFile->Data() + leaf.start, leaf.byteLength);
    }
    return std::string_view(backing.added->data() + leaf.start, leaf.byteLength);
}

std::shared_ptr<const PieceTable::Node> PieceTable::MakeLeaf(SpanSource source, std::size_t start, std::size_t length, const Backing& backing) {
    if (length == 0) {
        return nullptr;
    }

    auto node        = std::make_shared<Node>();
    node->source     = source;
    node->start      = start;
    node->byteLength = length;

    const std::string_view text = SpanView(*node, backing);
    node->codepointLength       = CountCodepoints(text);
    node->lineCount             = CountNewlines(text);
    node->depth                 = 0;

    return node;
}

std::shared_ptr<const PieceTable::Node> PieceTable::BuildBalancedSpan(SpanSource source, std::size_t start, std::size_t length, std::size_t chunkSize, const Backing& backing) {
    if (length == 0) {
        return nullptr;
    }

    const char* base = (source == SpanSource::kOriginal) ? backing.mappedFile->Data() : backing.added->data();

    std::vector<std::shared_ptr<const Node>> leaves;
    std::size_t                              offset = 0;

    while (offset < length) {
        std::size_t chunkEnd = std::min(offset + chunkSize, length);

        while (chunkEnd < length && IsContinuationByte(base[start + chunkEnd])) {
            --chunkEnd;
        }

        leaves.push_back(MakeLeaf(source, start + offset, chunkEnd - offset, backing));
        offset = chunkEnd;
    }

    return BuildBalanced(std::move(leaves));
}

std::shared_ptr<const PieceTable::Node> PieceTable::BuildBalanced(std::vector<std::shared_ptr<const Node>> leaves) {
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

std::shared_ptr<const PieceTable::Node> PieceTable::MakeInternal(std::shared_ptr<const Node> left, std::shared_ptr<const Node> right) {
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

std::shared_ptr<const PieceTable::Node> PieceTable::RotateLeft(const std::shared_ptr<const Node>& node) {
    auto pivot = node->right;
    return MakeInternal(MakeInternal(node->left, pivot->left), pivot->right);
}

std::shared_ptr<const PieceTable::Node> PieceTable::RotateRight(const std::shared_ptr<const Node>& node) {
    auto pivot = node->left;
    return MakeInternal(pivot->left, MakeInternal(pivot->right, node->right));
}

// AVL-style rebalance, same invariant/reasoning as Rope::Rebalance.
std::shared_ptr<const PieceTable::Node> PieceTable::Rebalance(std::shared_ptr<const Node> node) {
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

// Same O(|depth(left) - depth(right)|) join as Rope::Concat -- see that
// function's own comment for why a plain MakeInternal-plus-periodic-rebuild
// approach isn't used instead.
std::shared_ptr<const PieceTable::Node> PieceTable::Concat(std::shared_ptr<const Node> left, std::shared_ptr<const Node> right) {
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

std::pair<std::shared_ptr<const PieceTable::Node>, std::shared_ptr<const PieceTable::Node>>
PieceTable::Split(const std::shared_ptr<const Node>& node, std::size_t offset, const Backing& backing) {
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
        // No byte copy needed -- splitting a span descriptor is just two
        // adjusted {source, start, length} triples, unlike Rope's leaf case
        // (a real text::substr). MakeLeaf still resolves bytes once each to
        // recompute the two halves' codepoint/newline counts, since those
        // aren't derivable from the parent leaf's aggregate counts alone.
        return {MakeLeaf(node->source, node->start, offset, backing), MakeLeaf(node->source, node->start + offset, node->byteLength - offset, backing)};
    }

    const std::size_t leftLen = node->left->byteLength;

    if (offset < leftLen) {
        auto [ll, lr] = Split(node->left, offset, backing);
        return {ll, Concat(lr, node->right)};
    }
    if (offset > leftLen) {
        auto [rl, rr] = Split(node->right, offset - leftLen, backing);
        return {Concat(node->left, rl), rr};
    }
    return {node->left, node->right};
}

PieceTable PieceTable::Inserted(std::size_t byteOffset, std::string_view text) const {
    if (text.empty()) {
        return *this;
    }

    byteOffset = std::min(byteOffset, ByteLength());

    // Appends in place to the (possibly shared) added_ buffer -- see the
    // class comment for why this stays safe across every PieceTable
    // derived from the same origin: existing spans only ever reference a
    // strictly earlier prefix, which an append never disturbs.
    Backing newBacking = backing_;
    if (!newBacking.added) {
        newBacking.added = std::make_shared<std::string>();
    }
    const std::size_t insertStart = newBacking.added->size();
    newBacking.added->append(text);

    auto [left, right] = Split(root_, byteOffset, backing_);
    auto middle         = BuildBalancedSpan(SpanSource::kAdded, insertStart, text.size(), kAddedChunkSize, newBacking);

    return PieceTable(Concat(Concat(left, middle), right), newBacking);
}

PieceTable PieceTable::Erased(std::size_t byteOffset, std::size_t byteLength) const {
    byteOffset = std::min(byteOffset, ByteLength());
    byteLength = std::min(byteLength, ByteLength() - byteOffset);

    if (byteLength == 0) {
        return *this;
    }

    auto [left, rest]    = Split(root_, byteOffset, backing_);
    auto [middle, right] = Split(rest, byteLength, backing_);
    (void)middle;

    return PieceTable(Concat(left, right), backing_);
}

void PieceTable::AppendToString(const std::shared_ptr<const Node>& node, std::string& out, const Backing& backing) {
    if (!node) {
        return;
    }
    if (node->IsLeaf()) {
        out += SpanView(*node, backing);
        return;
    }
    AppendToString(node->left, out, backing);
    AppendToString(node->right, out, backing);
}

std::string PieceTable::ToString() const {
    std::string out;
    out.reserve(ByteLength());
    AppendToString(root_, out, backing_);
    return out;
}

std::string PieceTable::Substring(std::size_t byteOffset, std::size_t byteLength) const {
    byteOffset = std::min(byteOffset, ByteLength());
    byteLength = std::min(byteLength, ByteLength() - byteOffset);

    auto [unused, rest]  = Split(root_, byteOffset, backing_);
    auto [middle, right] = Split(rest, byteLength, backing_);
    (void)unused;
    (void)right;

    std::string out;
    out.reserve(byteLength);
    AppendToString(middle, out, backing_);
    return out;
}

void PieceTable::ForEachChunkImpl(const std::shared_ptr<const Node>& node, const Backing& backing, const std::function<void(std::string_view)>& sink) {
    if (!node) {
        return;
    }
    if (node->IsLeaf()) {
        sink(SpanView(*node, backing));
        return;
    }
    ForEachChunkImpl(node->left, backing, sink);
    ForEachChunkImpl(node->right, backing, sink);
}

void PieceTable::ForEachChunk(const std::function<void(std::string_view)>& sink) const {
    ForEachChunkImpl(root_, backing_, sink);
}

std::size_t PieceTable::CountNewlinesBefore(const std::shared_ptr<const Node>& node, std::size_t offset, const Backing& backing) {
    if (!node || offset == 0) {
        return 0;
    }
    if (node->IsLeaf()) {
        const std::size_t      limit = std::min(offset, node->byteLength);
        const std::string_view text  = SpanView(*node, backing);
        return static_cast<std::size_t>(std::count(text.begin(), text.begin() + static_cast<std::ptrdiff_t>(limit), '\n'));
    }

    const std::size_t leftLen = node->left->byteLength;
    if (offset <= leftLen) {
        return CountNewlinesBefore(node->left, offset, backing);
    }
    return node->left->lineCount + CountNewlinesBefore(node->right, offset - leftLen, backing);
}

std::size_t PieceTable::ByteOffsetToLine(std::size_t byteOffset) const {
    return CountNewlinesBefore(root_, std::min(byteOffset, ByteLength()), backing_);
}

std::size_t PieceTable::FindLineStart(const std::shared_ptr<const Node>& node, std::size_t line, const Backing& backing) {
    if (!node || line == 0) {
        return 0;
    }
    if (node->IsLeaf()) {
        const std::string_view text = SpanView(*node, backing);
        std::size_t            seen = 0;
        for (std::size_t i = 0; i < text.size(); ++i) {
            if (text[i] == '\n') {
                ++seen;
                if (seen == line) {
                    return i + 1;
                }
            }
        }
        return node->byteLength;
    }

    if (node->left->lineCount >= line) {
        return FindLineStart(node->left, line, backing);
    }
    return node->left->byteLength + FindLineStart(node->right, line - node->left->lineCount, backing);
}

std::size_t PieceTable::LineToByteOffset(std::size_t line) const {
    if (line == 0 || !root_) {
        return 0;
    }
    if (line > root_->lineCount) {
        return root_->byteLength;
    }
    return FindLineStart(root_, line, backing_);
}

std::size_t PieceTable::CountCodepointsBefore(const std::shared_ptr<const Node>& node, std::size_t offset, const Backing& backing) {
    if (!node || offset == 0) {
        return 0;
    }
    if (node->IsLeaf()) {
        const std::size_t      limit = std::min(offset, node->byteLength);
        const std::string_view text  = SpanView(*node, backing);
        std::size_t             count = 0;
        for (std::size_t i = 0; i < limit; ++i) {
            if (!IsContinuationByte(text[i])) {
                ++count;
            }
        }
        return count;
    }

    const std::size_t leftLen = node->left->byteLength;
    if (offset <= leftLen) {
        return CountCodepointsBefore(node->left, offset, backing);
    }
    return node->left->codepointLength + CountCodepointsBefore(node->right, offset - leftLen, backing);
}

std::size_t PieceTable::ByteOffsetToCodepointOffset(std::size_t byteOffset) const {
    return CountCodepointsBefore(root_, std::min(byteOffset, ByteLength()), backing_);
}

std::size_t PieceTable::FindCodepointStart(const std::shared_ptr<const Node>& node, std::size_t codepointOffset, const Backing& backing) {
    if (!node || codepointOffset == 0) {
        return 0;
    }
    if (node->IsLeaf()) {
        const std::string_view text = SpanView(*node, backing);
        std::size_t             seen = 0;
        for (std::size_t i = 0; i < text.size(); ++i) {
            if (!IsContinuationByte(text[i])) {
                if (seen == codepointOffset) {
                    return i;
                }
                ++seen;
            }
        }
        return node->byteLength;
    }

    if (node->left->codepointLength >= codepointOffset) {
        return FindCodepointStart(node->left, codepointOffset, backing);
    }
    return node->left->byteLength + FindCodepointStart(node->right, codepointOffset - node->left->codepointLength, backing);
}

std::size_t PieceTable::CodepointOffsetToByteOffset(std::size_t codepointOffset) const {
    if (codepointOffset == 0 || !root_) {
        return 0;
    }
    if (codepointOffset >= root_->codepointLength) {
        return root_->byteLength;
    }
    return FindCodepointStart(root_, codepointOffset, backing_);
}

std::uint8_t PieceTable::ByteAt(const std::shared_ptr<const Node>& node, std::size_t offset, const Backing& backing) {
    if (node->IsLeaf()) {
        return static_cast<std::uint8_t>(SpanView(*node, backing)[offset]);
    }

    const std::size_t leftLen = node->left->byteLength;
    if (offset < leftLen) {
        return ByteAt(node->left, offset, backing);
    }
    return ByteAt(node->right, offset - leftLen, backing);
}

PieceTable::DecodedCodepoint PieceTable::CodepointAt(std::size_t byteOffset) const {
    const std::size_t total = ByteLength();
    if (byteOffset >= total) {
        return {0xFFFD, 0};
    }

    const std::uint8_t b0 = ByteAt(root_, byteOffset, backing_);

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
        const std::uint8_t b = ByteAt(root_, byteOffset + i, backing_);
        if (!IsContinuationByte(static_cast<char>(b))) {
            return {0xFFFD, 1};
        }
        cp = (cp << 6) | (b & 0x3F);
    }

    return {cp, len};
}

std::size_t PieceTable::PreviousCodepointBoundary(std::size_t byteOffset) const {
    if (byteOffset == 0) {
        return 0;
    }

    std::size_t offset = byteOffset - 1;
    std::size_t steps  = 0;

    while (offset > 0 && IsContinuationByte(static_cast<char>(ByteAt(root_, offset, backing_))) && steps < 3) {
        --offset;
        ++steps;
    }

    return offset;
}

std::size_t PieceTable::NextCodepointBoundary(std::size_t byteOffset) const {
    const std::size_t total = ByteLength();
    if (byteOffset >= total) {
        return total;
    }
    return byteOffset + CodepointAt(byteOffset).byteLength;
}

} // namespace ned::text
