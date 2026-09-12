#include "StickyScroll.h"

namespace ned::editor::stickyscroll {

std::vector<SymbolMarker> EnclosingSymbolChain(const std::vector<SymbolMarker>& markers, std::size_t point) {
    std::vector<SymbolMarker> chain;
    for (const SymbolMarker& marker : markers) {
        if (marker.startByte <= point && point < marker.endByte) {
            chain.push_back(marker);
        }
    }
    return chain;
}

std::vector<SymbolMarker> StickyChainForViewportTop(const std::vector<SymbolMarker>& markers, std::size_t viewportTopByte) {
    std::vector<SymbolMarker> chain;
    for (const SymbolMarker& marker : markers) {
        if (marker.startByte < viewportTopByte && viewportTopByte < marker.endByte) {
            chain.push_back(marker);
        }
    }
    return chain;
}

std::vector<SymbolMarker> MarkersFromFoldBlocks(const std::vector<std::pair<std::size_t, std::size_t>>& blocks) {
    std::vector<SymbolMarker> markers;
    markers.reserve(blocks.size());
    for (const auto& [startByte, endByte] : blocks) {
        // No name and no name offset: the sticky row renders the block's own
        // source line verbatim (BufferView::PaintStickyScrollRows), which is
        // what every sticky-scroll implementation shows anyway, so there is
        // nothing here to invent an identifier for.
        markers.push_back(SymbolMarker{.startByte     = startByte,
                                       .endByte       = endByte,
                                       .kind          = SymbolKind::Block,
                                       .name          = {},
                                       .nameStartByte = startByte});
    }
    return markers;
}

} // namespace ned::editor::stickyscroll
