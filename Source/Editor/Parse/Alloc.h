#pragma once

#include <cstddef>
#include <cstdlib>
#include <new>

// The engine's C heap, failing the way operator new does: std::bad_alloc
// rather than a null pointer the C port would go on to write through. A
// failed realloc leaves the original block untouched and owned by the caller.

namespace ned::editor::parse {

[[nodiscard]] inline void* CheckedMalloc(std::size_t size) {
    void* const block = std::malloc(size);
    if (block == nullptr && size != 0) {
        throw std::bad_alloc();
    }
    return block;
}

[[nodiscard]] inline void* CheckedCalloc(std::size_t count, std::size_t size) {
    void* const block = std::calloc(count, size);
    if (block == nullptr && count != 0 && size != 0) {
        throw std::bad_alloc();
    }
    return block;
}

[[nodiscard]] inline void* CheckedRealloc(void* block, std::size_t size) {
    void* const grown = std::realloc(block, size);
    if (grown == nullptr && size != 0) {
        throw std::bad_alloc();
    }
    return grown;
}

} // namespace ned::editor::parse
