//
// A map from a live Buffer to per-buffer state. Keyed by address, but an
// entry only belongs to a buffer whose InstanceId() matches the one it was
// stored under: a buffer freed without its entry being erased must not hand
// that entry to whatever is later allocated at the same address.
//

#ifndef NED_TEXT_PER_BUFFER_MAP_H
#define NED_TEXT_PER_BUFFER_MAP_H

#include <cstddef>
#include <unordered_map>
#include <utility>

#include "Text/Buffer.h"

namespace ned::text {

template <typename T>
class PerBufferMap {
  public:
    // buffer's entry, or nullptr; a stale entry at buffer's address is dropped.
    [[nodiscard]] T* Find(const Buffer& buffer) {
        const auto it = entries_.find(&buffer);
        if (it == entries_.end()) {
            return nullptr;
        }
        if (it->second.instanceId != buffer.InstanceId()) {
            entries_.erase(it);
            return nullptr;
        }
        return &it->second.value;
    }

    T& Set(const Buffer& buffer, T value) {
        Entry& entry = entries_.insert_or_assign(&buffer, Entry{buffer.InstanceId(), std::move(value)}).first->second;
        return entry.value;
    }

    // Stores value only when buffer has no live entry.
    T& SetIfAbsent(const Buffer& buffer, T value) {
        if (T* existing = Find(buffer)) {
            return *existing;
        }
        return Set(buffer, std::move(value));
    }

    void Erase(const Buffer& buffer) {
        entries_.erase(&buffer);
    }

    void Clear() {
        entries_.clear();
    }

    [[nodiscard]] std::size_t Size() const {
        return entries_.size();
    }

  private:
    struct Entry {
        std::size_t instanceId = 0;
        T           value;
    };

    std::unordered_map<const Buffer*, Entry> entries_;
};

} // namespace ned::text

#endif // NED_TEXT_PER_BUFFER_MAP_H
