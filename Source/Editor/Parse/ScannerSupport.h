#pragma once

#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>

// The growable array a scanner keeps its state in (a stack of heredoc
// delimiters, of indentation levels, of open brackets). Plain C-style
// storage on purpose: a scanner's state is serialized as bytes and its
// code is written in the C dialect the grammars ship, so this is the
// tree-sitter `array.h` vocabulary made to compile as C++ -- the private
// helpers are typed, and allocation is the C heap directly.

namespace ned::editor::parse::scanner {

// C lets a void* become any object pointer; C++ does not. The scanners
// are written the C way (the opaque payload, the heap), so their entry
// points receive the payload through this and allocate through the
// wrappers below, and the conversion happens where the C compiler would
// have done it silently.
struct VoidPtr {
    void* p;
    template <typename T>
    operator T*() const {
        return static_cast<T*>(p);
    }
    explicit operator bool() const {
        return p != nullptr;
    }
};

inline VoidPtr scanner_malloc(size_t size) {
    return {std::malloc(size)};
}
inline VoidPtr scanner_calloc(size_t count, size_t size) {
    return {std::calloc(count, size)};
}
inline VoidPtr scanner_realloc(void* pointer, size_t size) {
    return {std::realloc(pointer, size)};
}

#define Array(T)           \
    struct {               \
        T*       contents; \
        uint32_t size;     \
        uint32_t capacity; \
    }

#define array_init(self) ((self)->size = 0, (self)->capacity = 0, (self)->contents = NULL)
#define array_new() \
    {NULL, 0, 0}
#define array_get(self, _index) (assert((uint32_t)(_index) < (self)->size), &(self)->contents[_index])
#define array_front(self) array_get(self, 0)
#define array_back(self) array_get(self, (self)->size - 1)
#define array_clear(self) ((self)->size = 0)
#define array_reserve(self, new_capacity) _array__reserve((Array*)(self), array_elem_size(self), new_capacity)
#define array_delete(self) _array__delete((Array*)(self))
#define array_push(self, element) (_array__grow((Array*)(self), 1, array_elem_size(self)), (self)->contents[(self)->size++] = (element))
#define array_grow_by(self, count)                                                   \
    do {                                                                             \
        if ((count) == 0)                                                            \
            break;                                                                   \
        _array__grow((Array*)(self), count, array_elem_size(self));                  \
        memset((self)->contents + (self)->size, 0, (count) * array_elem_size(self)); \
        (self)->size += (count);                                                     \
    }                                                                                \
    while (0)
#define array_push_all(self, other) array_extend((self), (other)->size, (other)->contents)
#define array_extend(self, count, contents) _array__splice((Array*)(self), array_elem_size(self), (self)->size, 0, count, contents)
#define array_splice(self, _index, old_count, new_count, new_contents) \
    _array__splice((Array*)(self), array_elem_size(self), _index, old_count, new_count, new_contents)
#define array_insert(self, _index, element) _array__splice((Array*)(self), array_elem_size(self), _index, 0, 1, &(element))
#define array_erase(self, _index) _array__erase((Array*)(self), array_elem_size(self), _index)
#define array_pop(self) ((self)->contents[--(self)->size])
#define array_assign(self, other) _array__assign((Array*)(self), (const Array*)(other), array_elem_size(self))
#define array_swap(self, other) _array__swap((Array*)(self), (Array*)(other))
#define array_elem_size(self) (sizeof *(self)->contents)
#define array_search_sorted_with(self, compare, needle, _index, _exists) _array__search_sorted(self, 0, compare, , needle, _index, _exists)
#define array_search_sorted_by(self, field, needle, _index, _exists) _array__search_sorted(self, 0, _compare_int, field, needle, _index, _exists)
#define array_insert_sorted_with(self, compare, value)                        \
    do {                                                                      \
        unsigned _index, _exists;                                             \
        array_search_sorted_with(self, compare, &(value), &_index, &_exists); \
        if (!_exists)                                                         \
            array_insert(self, _index, value);                                \
    }                                                                         \
    while (0)
#define array_insert_sorted_by(self, field, value)                            \
    do {                                                                      \
        unsigned _index, _exists;                                             \
        array_search_sorted_by(self, field, (value)field, &_index, &_exists); \
        if (!_exists)                                                         \
            array_insert(self, _index, value);                                \
    }                                                                         \
    while (0)

typedef Array(void) Array;

inline void _array__delete(Array* self) {
    if (self->contents) {
        free(self->contents);
        self->contents = NULL;
        self->size     = 0;
        self->capacity = 0;
    }
}

inline void _array__erase(Array* self, size_t element_size, uint32_t index) {
    assert(index < self->size);
    char* contents = (char*)self->contents;
    memmove(contents + index * element_size, contents + (index + 1) * element_size, (self->size - index - 1) * element_size);
    self->size--;
}

inline void _array__reserve(Array* self, size_t element_size, uint32_t new_capacity) {
    if (new_capacity > self->capacity) {
        if (self->contents) {
            self->contents = std::realloc(self->contents, new_capacity * element_size);
        }
        else {
            self->contents = std::malloc(new_capacity * element_size);
        }
        self->capacity = new_capacity;
    }
}

inline void _array__assign(Array* self, const Array* other, size_t element_size) {
    _array__reserve(self, element_size, other->size);
    self->size = other->size;
    memcpy(self->contents, other->contents, self->size * element_size);
}

inline void _array__swap(Array* self, Array* other) {
    Array swap = *other;
    *other     = *self;
    *self      = swap;
}

inline void _array__grow(Array* self, uint32_t count, size_t element_size) {
    uint32_t new_size = self->size + count;
    if (new_size > self->capacity) {
        uint32_t new_capacity = self->capacity * 2;
        if (new_capacity < 8)
            new_capacity = 8;
        if (new_capacity < new_size)
            new_capacity = new_size;
        _array__reserve(self, element_size, new_capacity);
    }
}

inline void _array__splice(Array* self, size_t element_size, uint32_t index, uint32_t old_count, uint32_t new_count, const void* elements) {
    uint32_t new_size = self->size + new_count - old_count;
    uint32_t old_end  = index + old_count;
    uint32_t new_end  = index + new_count;
    assert(old_end <= self->size);
    _array__reserve(self, element_size, new_size);
    char* contents = (char*)self->contents;
    if (self->size > old_end) {
        memmove(contents + new_end * element_size, contents + old_end * element_size, (self->size - old_end) * element_size);
    }
    if (new_count > 0) {
        if (elements) {
            memcpy((contents + index * element_size), elements, new_count * element_size);
        }
        else {
            memset((contents + index * element_size), 0, new_count * element_size);
        }
    }
    self->size += new_count - old_count;
}

#define _array__search_sorted(self, start, compare, suffix, needle, _index, _exists)       \
    do {                                                                                   \
        *(_index)     = start;                                                             \
        *(_exists)    = false;                                                             \
        uint32_t size = (self)->size - *(_index);                                          \
        if (size == 0)                                                                     \
            break;                                                                         \
        int comparison;                                                                    \
        while (size > 1) {                                                                 \
            uint32_t half_size = size / 2;                                                 \
            uint32_t mid_index = *(_index) + half_size;                                    \
            comparison         = compare(&((self)->contents[mid_index] suffix), (needle)); \
            if (comparison <= 0)                                                           \
                *(_index) = mid_index;                                                     \
            size -= half_size;                                                             \
        }                                                                                  \
        comparison = compare(&((self)->contents[*(_index)] suffix), (needle));             \
        if (comparison == 0)                                                               \
            *(_exists) = true;                                                             \
        else if (comparison < 0)                                                           \
            *(_index) += 1;                                                                \
    }                                                                                      \
    while (0)

#define _compare_int(a, b) ((int)*(a) - (int)(b))

} // namespace ned::editor::parse::scanner
