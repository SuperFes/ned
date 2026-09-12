#include "CodeFold.h"

#include <algorithm>
#include <optional>

namespace ned::editor::codefold {

std::vector<std::pair<std::size_t, std::size_t>> FoldableBlocks(const Mode& mode, std::string_view bufferText) {
    if (!mode.fold) {
        return {};
    }
    std::vector<std::pair<std::size_t, std::size_t>> blocks = mode.fold(bufferText);

    // A fold must span more than one line, and this is the one place to say
    // so: every consumer -- the gutter affordance, ToggleFoldAtLine,
    // FoldedLineRanges -- comes through here, and they had drifted apart
    // (the gutter refused a single-line block; the toggle did not, and
    // folding one hid the line below it).
    //
    // It is a property of the TEXT rather than of the grammar, which is why
    // no fold source can answer it: the same node type is foldable or not
    // depending on how it was written. That makes this the natural home for
    // it, and it is what lets sources compose freely -- a source may report
    // an empty `()` parameter list without that becoming a fold affordance.
    std::erase_if(blocks, [bufferText](const std::pair<std::size_t, std::size_t>& block) {
        const std::size_t start = std::min(block.first, bufferText.size());
        const std::size_t end   = std::min(block.second, bufferText.size());
        return start >= end || bufferText.find('\n', start) >= end;
    });
    return blocks;
}

std::vector<std::pair<std::size_t, std::size_t>>
FoldedLineRanges(const text::Buffer& buffer, const text::ITextStorage& content,
                 const std::vector<std::pair<std::size_t, std::size_t>>& blocks) {
    std::vector<std::pair<std::size_t, std::size_t>> ranges;
    if (buffer.FoldMarkers().empty() || blocks.empty()) {
        return ranges;
    }

    for (const auto& [byteOffset, marker] : buffer.FoldMarkers()) {
        if (marker != text::Buffer::FoldMarker::Collapsed) {
            continue;
        }
        const auto it = std::lower_bound(
            blocks.begin(), blocks.end(), byteOffset,
            [](const std::pair<std::size_t, std::size_t>& block, std::size_t offset) { return block.first < offset; });
        if (it == blocks.end() || it->first != byteOffset) {
            continue; // stale marker -- no matching foldable block anymore
        }
        const std::size_t startLine = content.ByteOffsetToLine(it->first);
        const std::size_t endLine   = content.ByteOffsetToLine(it->second);
        if (endLine <= startLine) {
            // A block confined to one line has nothing of its own to hide,
            // and [startLine + 1, endLine + 1] would name the line *after*
            // it. Real and reachable with the queries as they stand:
            // `(compound_statement) @fold` matches the one-line body in
            // `int f(void) { return 1; }`, and folding it hid the next
            // function's opening line.
            continue;
        }
        ranges.emplace_back(startLine + 1, endLine + 1);
    }
    return ranges;
}

bool ToggleFoldAtLine(text::Buffer& buffer, const text::ITextStorage& content,
                      const std::vector<std::pair<std::size_t, std::size_t>>& blocks, std::size_t line) {
    const std::pair<std::size_t, std::size_t>* best = nullptr;
    for (const auto& block : blocks) {
        if (content.ByteOffsetToLine(block.first) != line) {
            continue;
        }
        if (content.ByteOffsetToLine(block.second) <= line) {
            continue; // single-line block -- see FoldedLineRanges' own note
        }
        if (best == nullptr || (block.second - block.first) > (best->second - best->first)) {
            best = &block;
        }
    }
    if (best == nullptr) {
        return false;
    }

    const bool collapsed = buffer.FoldMarkerAt(best->first).has_value();
    buffer.SetFoldMarker(best->first, collapsed ? std::nullopt : std::optional(text::Buffer::FoldMarker::Collapsed));
    return true;
}

std::vector<FoldRegion> FoldRegionsWithDepth(const std::vector<std::pair<std::size_t, std::size_t>>& blocks) {
    std::vector<FoldRegion> regions;
    regions.reserve(blocks.size());

    std::vector<std::size_t> openEndBytes; // endByte of each still-open ancestor
    for (const auto& [start, end] : blocks) {
        while (!openEndBytes.empty() && openEndBytes.back() <= start) {
            openEndBytes.pop_back();
        }
        regions.push_back(FoldRegion{.startByte = start, .endByte = end, .depth = static_cast<int>(openEndBytes.size())});
        openEndBytes.push_back(end);
    }
    return regions;
}

} // namespace ned::editor::codefold
