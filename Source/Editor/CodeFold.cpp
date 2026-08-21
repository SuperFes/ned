#include "CodeFold.h"

#include <algorithm>
#include <optional>

namespace ned::editor::codefold {

std::vector<std::pair<std::size_t, std::size_t>> FoldableBlocks(const Mode& mode, std::string_view bufferText) {
    if (!mode.fold) {
        return {};
    }
    return mode.fold(bufferText);
}

std::vector<std::pair<std::size_t, std::size_t>>
FoldedLineRanges(const text::Buffer& buffer, const text::Rope& content,
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
        ranges.emplace_back(startLine + 1, endLine + 1);
    }
    return ranges;
}

bool ToggleFoldAtLine(text::Buffer& buffer, const text::Rope& content,
                      const std::vector<std::pair<std::size_t, std::size_t>>& blocks, std::size_t line) {
    const std::pair<std::size_t, std::size_t>* best = nullptr;
    for (const auto& block : blocks) {
        if (content.ByteOffsetToLine(block.first) != line) {
            continue;
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
