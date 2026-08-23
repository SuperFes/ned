#include "IncrementalParse.h"

#include <algorithm>

namespace ned::editor::treesitter {

namespace {

    // Row/column of byte offset `offset` within `text`, in tree-sitter's own
    // TSPoint terms (0-indexed row, byte-indexed column within that row --
    // matching the codepoint-agnostic byte offsets this project's own
    // TreeSitter layer already uses throughout). A plain linear scan, not
    // reused/cached across calls -- IncrementalParseCache calls this at most
    // three times per edit, each bounded by the edit's own offset into text
    // that's already fully resident, cheap next to the parse it precedes.
    TSPoint PointForByteOffset(std::string_view text, std::size_t offset) {
        uint32_t    row       = 0;
        std::size_t lineStart = 0;
        for (std::size_t i = 0; i < offset; ++i) {
            if (text[i] == '\n') {
                ++row;
                lineStart = i + 1;
            }
        }
        return TSPoint{.row = row, .column = static_cast<uint32_t>(offset - lineStart)};
    }

} // namespace

const Tree& IncrementalParseCache::Update(const Parser& parser, std::string_view bufferText) {
    if (lastTree_.has_value() && lastText_ == bufferText) {
        return *lastTree_;
    }

    if (!lastTree_.has_value()) {
        lastTree_ = parser.Parse(bufferText);
        lastText_.assign(bufferText);
        return *lastTree_;
    }

    const std::string_view oldText = lastText_;
    const std::string_view newText = bufferText;

    const std::size_t maxCommon = std::min(oldText.size(), newText.size());
    std::size_t       prefix    = 0;
    while (prefix < maxCommon && oldText[prefix] == newText[prefix]) {
        ++prefix;
    }
    const std::size_t maxSuffix = maxCommon - prefix; // caps prefix+suffix at maxCommon, so they can't overlap
    std::size_t        suffix    = 0;
    while (suffix < maxSuffix && oldText[oldText.size() - 1 - suffix] == newText[newText.size() - 1 - suffix]) {
        ++suffix;
    }

    const std::size_t startByte  = prefix;
    const std::size_t oldEndByte = oldText.size() - suffix;
    const std::size_t newEndByte = newText.size() - suffix;

    TSInputEdit edit{};
    edit.start_byte    = static_cast<uint32_t>(startByte);
    edit.old_end_byte  = static_cast<uint32_t>(oldEndByte);
    edit.new_end_byte  = static_cast<uint32_t>(newEndByte);
    edit.start_point   = PointForByteOffset(oldText, startByte);
    edit.old_end_point = PointForByteOffset(oldText, oldEndByte);
    edit.new_end_point = PointForByteOffset(newText, newEndByte);

    lastTree_->Edit(edit);
    lastTree_ = parser.Parse(newText, *lastTree_);
    lastText_.assign(newText);
    return *lastTree_;
}

} // namespace ned::editor::treesitter
