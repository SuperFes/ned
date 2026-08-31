//
// Persistent piece table over a read-only mmap'd original file plus a small
// append-only in-memory buffer for inserted text.
//

#ifndef NED_TEXT_PIECETABLE_H
#define NED_TEXT_PIECETABLE_H

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ned::text {

class MappedFile;

// Rope's public surface mirrored deliberately (see Rope.h) so Buffer's
// storage abstraction can swap between the two without callers noticing,
// but backed differently: a leaf here is a {source, start, length} span
// descriptor into the mmap or the append buffer, never owned inline text,
// so opening or editing a multi-GB file never requires copying it into
// memory first. See the huge-file-editing plan (huge-file opening +
// editing follow-up) for the full design rationale, and Rope.h's own
// comments for why the tree-balancing shape here is a deliberate,
// independent duplication of Rope's rather than a shared abstraction --
// Rope is a performance-sensitive, already-tuned hot path every normal
// buffer depends on, and isn't worth risking to grow a leaf variant it was
// never designed for.
//
// Editing operations (Inserted/Erased) return a new PieceTable rather than
// mutating this one, exactly like Rope -- unchanged subtrees are shared via
// shared_ptr. Deleted text is never physically removed from the mmap or the
// append buffer, only unreferenced by the tree: the original file is never
// written to, and the append buffer is never compacted/reclaimed, so memory
// cost is proportional to the total text ever inserted across the whole
// edit history (not the file size, and not just the currently-live
// content). Fine for a normal editing session; not attempting compaction in
// v1 -- see the plan's own note on this trade-off.
//
// Not thread-safe, same contract as Rope and the rest of Text/ -- all
// access is expected from the main thread, exactly like Buffer itself.
class PieceTable {
  public:
    PieceTable();

    // Opens path read-only via mmap (MappedFile::Open -- throws
    // MappedFileError on failure, never silently falls back to a full
    // read) and does one O(file size) linear byte-scan to seed
    // newline/codepoint counts for a single span covering the whole file.
    // No file content is copied; open time is dominated by that one linear
    // scan (comparable cost to `wc -l`), not by anything proportional to a
    // per-edit cost.
    static PieceTable FromFile(const std::filesystem::path& path);

    [[nodiscard]] bool        Empty() const;
    [[nodiscard]] std::size_t ByteLength() const;
    [[nodiscard]] std::size_t CodepointLength() const;
    [[nodiscard]] std::size_t LineCount() const; // newline count + 1

    [[nodiscard]] PieceTable Inserted(std::size_t byteOffset, std::string_view text) const;
    [[nodiscard]] PieceTable Erased(std::size_t byteOffset, std::size_t byteLength) const;

    // Whole-document materialize -- expensive for a huge table (copies
    // every byte). Callers touching a huge buffer should prefer Substring
    // or ForEachChunk; kept for parity with Rope::ToString and for the
    // small/empty case (tests, a table that never grew past a trivial
    // size).
    [[nodiscard]] std::string ToString() const;
    [[nodiscard]] std::string Substring(std::size_t byteOffset, std::size_t byteLength) const;

    // 0-indexed line number containing byteOffset / byte offset where a
    // given 0-indexed line starts.
    [[nodiscard]] std::size_t ByteOffsetToLine(std::size_t byteOffset) const;
    [[nodiscard]] std::size_t LineToByteOffset(std::size_t line) const;

    [[nodiscard]] std::size_t ByteOffsetToCodepointOffset(std::size_t byteOffset) const;
    [[nodiscard]] std::size_t CodepointOffsetToByteOffset(std::size_t codepointOffset) const;

    struct DecodedCodepoint {
        char32_t    codepoint;
        std::size_t byteLength;
    };

    // Decodes the codepoint starting at byteOffset. Malformed/truncated
    // UTF-8 decodes as U+FFFD with byteLength >= 1, so callers always make
    // forward progress -- same contract as Rope::CodepointAt.
    [[nodiscard]] DecodedCodepoint CodepointAt(std::size_t byteOffset) const;

    [[nodiscard]] std::size_t PreviousCodepointBoundary(std::size_t byteOffset) const;
    [[nodiscard]] std::size_t NextCodepointBoundary(std::size_t byteOffset) const;

    // Streaming save follow-up (Buffer::SaveToFile's huge-buffer path):
    // invokes sink once per leaf span, left to right, without ever
    // materializing the whole document into one string. Each call hands a
    // view directly into the mmap (an Original span) or the append buffer
    // (an Added span) -- valid only for the duration of that one call, the
    // same per-call-only validity convention ProjectSearch's own
    // per-match callbacks already use.
    void ForEachChunk(const std::function<void(std::string_view)>& sink) const;

  private:
    struct Node;

    enum class SpanSource : std::uint8_t { kOriginal, kAdded };

    // Bundles the two pieces of storage a leaf's span may resolve against,
    // threaded explicitly through every static helper below rather than
    // read off `this` -- Inserted() needs to resolve a freshly-created
    // leaf's bytes against a *new* added_ buffer before that buffer is
    // installed as the returned PieceTable's own added_, so reading `this->
    // added_` inside a helper would see the wrong (stale, possibly still
    // null) buffer in that one case. Threading it explicitly sidesteps the
    // bug entirely rather than relying on call-order care.
    struct Backing {
        std::shared_ptr<const MappedFile> mappedFile;
        std::shared_ptr<std::string>      added;
    };

    PieceTable(std::shared_ptr<const Node> root, Backing backing);

    std::shared_ptr<const Node> root_;
    Backing                     backing_;

    static std::string_view SpanView(const Node& leaf, const Backing& backing);

    static std::shared_ptr<const Node> MakeLeaf(SpanSource source, std::size_t start, std::size_t length, const Backing& backing);
    // Chunks [start, start+length) of one source into several leaves at
    // roughly chunkSize each. Unlike Rope::BuildBalanced, a leaf here holds
    // no text of its own -- Split never copies bytes, only recomputes a
    // split leaf's codepoint/newline counts by rescanning its own span --
    // so the tradeoff Rope's small fixed chunk size exists for (bounding a
    // leaf-copy's cost) doesn't apply. What does still matter: (1) a split
    // near freshly-opened, never-before-split territory costs O(chunkSize)
    // to rescan, so chunkSize shouldn't be huge, and (2) each leaf is a
    // real heap-allocated Node regardless of how many bytes it describes,
    // so a chunk size tuned for Rope's inline-text leaves (512 bytes) would
    // spend node-object overhead roughly proportional to file size / 512 --
    // for a multi-GB file that's real, avoidable resident memory having
    // nothing to do with the file's own content. See the two call sites
    // (FromFile, Inserted) for why they intentionally pass different sizes.
    static std::shared_ptr<const Node> BuildBalancedSpan(SpanSource source, std::size_t start, std::size_t length, std::size_t chunkSize, const Backing& backing);
    static std::shared_ptr<const Node> BuildBalanced(std::vector<std::shared_ptr<const Node>> leaves);
    static std::shared_ptr<const Node> MakeInternal(std::shared_ptr<const Node> left, std::shared_ptr<const Node> right);
    static std::shared_ptr<const Node> RotateLeft(const std::shared_ptr<const Node>& node);
    static std::shared_ptr<const Node> RotateRight(const std::shared_ptr<const Node>& node);
    static std::shared_ptr<const Node> Rebalance(std::shared_ptr<const Node> node);
    static std::shared_ptr<const Node> Concat(std::shared_ptr<const Node> left, std::shared_ptr<const Node> right);
    static std::pair<std::shared_ptr<const Node>, std::shared_ptr<const Node>> Split(const std::shared_ptr<const Node>& node, std::size_t byteOffset, const Backing& backing);
    static void AppendToString(const std::shared_ptr<const Node>& node, std::string& out, const Backing& backing);
    static void ForEachChunkImpl(const std::shared_ptr<const Node>& node, const Backing& backing, const std::function<void(std::string_view)>& sink);

    static std::size_t  CountNewlinesBefore(const std::shared_ptr<const Node>& node, std::size_t byteOffset, const Backing& backing);
    static std::size_t  FindLineStart(const std::shared_ptr<const Node>& node, std::size_t line, const Backing& backing);
    static std::size_t  CountCodepointsBefore(const std::shared_ptr<const Node>& node, std::size_t byteOffset, const Backing& backing);
    static std::size_t  FindCodepointStart(const std::shared_ptr<const Node>& node, std::size_t codepointOffset, const Backing& backing);
    static std::uint8_t ByteAt(const std::shared_ptr<const Node>& node, std::size_t byteOffset, const Backing& backing);
};

} // namespace ned::text

#endif // NED_TEXT_PIECETABLE_H
