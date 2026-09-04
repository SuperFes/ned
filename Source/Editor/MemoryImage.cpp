#include "MemoryImage.h"

#include <algorithm>
#include <cmath>

namespace ned::editor {

MemoryImageLayout ComputeMemoryImageLayout(std::size_t byteCount, std::size_t maxColumns) {
    if (byteCount == 0 || maxColumns == 0) {
        return {};
    }

    std::size_t columns = static_cast<std::size_t>(std::ceil(std::sqrt(static_cast<double>(byteCount))));
    columns             = std::max<std::size_t>(columns, 1);
    columns             = std::min(columns, maxColumns);
    columns             = std::min(columns, byteCount);

    const std::size_t rows = (byteCount + columns - 1) / columns;
    return MemoryImageLayout{.pixelColumns = columns, .pixelRows = rows};
}

MemoryImageColor ByteToGrayscale(std::uint8_t byte) {
    return MemoryImageColor{.r = byte, .g = byte, .b = byte};
}

} // namespace ned::editor
