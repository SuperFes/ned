#include "ChunkedHighlight.h"

#include <algorithm>

namespace ned::editor {

std::size_t HighlightSweepChunk(const HighlightFunction& highlight, std::string_view text, std::size_t cursor,
                                std::size_t chunkBytes, std::vector<HighlightSpan>& out) {
    const std::size_t     chunkEnd = std::min(cursor + chunkBytes, text.size());
    const HighlightWindow window{.startByte = cursor, .endByte = chunkEnd};
    for (const HighlightSpan& span : highlight(text, window)) {
        if (span.startByte >= cursor) {
            out.push_back(span);
        }
    }
    return chunkEnd;
}

} // namespace ned::editor
