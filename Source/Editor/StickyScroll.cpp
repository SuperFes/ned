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

} // namespace ned::editor::stickyscroll
