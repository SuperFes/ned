#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstring>

// A raw-buffer growable array, port of tree-sitter's array.h. Deliberately
// not std::vector: Green.cpp's NewNode takes ownership of a SubtreeArray's
// buffer and constructs the node header inside it (children-before-header
// single allocation), which requires donating the raw buffer. Engine-internal
// only; every use is exercised under ASan by the conformance suites.

namespace ned::editor::parse {

template <typename T>
struct RawArray {
    T*            contents = nullptr;
    std::uint32_t size     = 0;
    std::uint32_t capacity = 0;

    void Delete() {
        std::free(contents);
        contents = nullptr;
        size     = 0;
        capacity = 0;
    }

    void Clear() {
        size = 0;
    }

    void Reserve(std::uint32_t newCapacity) {
        if (newCapacity > capacity) {
            contents = static_cast<T*>(std::realloc(contents, newCapacity * sizeof(T)));
            capacity = newCapacity;
        }
    }

    void GrowBy(std::uint32_t count) {
        if (size + count > capacity) {
            std::uint32_t newCapacity = capacity * 2;
            if (newCapacity < 8)
                newCapacity = 8;
            if (newCapacity < size + count)
                newCapacity = size + count;
            Reserve(newCapacity);
        }
    }

    void Push(T element) {
        GrowBy(1);
        contents[size++] = element;
    }

    T Pop() {
        return contents[--size];
    }

    T& Back() {
        return contents[size - 1];
    }
    T& Front() {
        return contents[0];
    }
    T& operator[](std::uint32_t index) {
        return contents[index];
    }
    const T& operator[](std::uint32_t index) const {
        return contents[index];
    }

    void Erase(std::uint32_t index) {
        std::memmove(contents + index, contents + index + 1, (size - index - 1) * sizeof(T));
        size--;
    }

    void Insert(std::uint32_t index, T element) {
        GrowBy(1);
        std::memmove(contents + index + 1, contents + index, (size - index) * sizeof(T));
        contents[index] = element;
        size++;
    }

    // Replace `oldCount` elements at `index` with `newCount` elements from `elements`.
    void Splice(std::uint32_t index, std::uint32_t oldCount, std::uint32_t newCount, const T* elements) {
        const std::uint32_t newSize = size + newCount - oldCount;
        const std::uint32_t oldEnd  = index + oldCount;
        const std::uint32_t newEnd  = index + newCount;
        GrowBy(newCount > oldCount ? newCount - oldCount : 0);
        std::memmove(contents + newEnd, contents + oldEnd, (size - oldEnd) * sizeof(T));
        if (elements != nullptr)
            std::memcpy(contents + index, elements, newCount * sizeof(T));
        size = newSize;
    }

    // Overwrite with a copy of another array's contents (shallow).
    void Assign(const RawArray<T>& other) {
        Clear();
        GrowBy(other.size);
        std::memcpy(contents, other.contents, other.size * sizeof(T));
        size = other.size;
    }

    void Swap(RawArray<T>& other) {
        const RawArray<T> tmp = *this;
        *this                 = other;
        other                 = tmp;
    }
};

} // namespace ned::editor::parse
