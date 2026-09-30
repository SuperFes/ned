//
// A map from a live Buffer to per-buffer state. Entries are keyed by
// address and erased when their buffer is destroyed, however it is
// destroyed, so a buffer allocated later at the same address never
// inherits one. An entry also remembers its buffer's InstanceId(): a buffer
// move-assigned into takes on the source's identity, and the entry stored
// for the old one no longer applies.
//
// Not thread-safe; the owner's own discipline applies. A buffer is erased
// on the thread that destroys it, under `guard` when one is given.
//

#ifndef NED_TEXT_PER_BUFFER_MAP_H
#define NED_TEXT_PER_BUFFER_MAP_H

#include <cstddef>
#include <mutex>
#include <unordered_map>
#include <utility>

#include "Text/Buffer.h"
#include "Text/BufferEntryOwners.h"

namespace ned::text {

template <typename T>
class PerBufferMap final : public BufferEntryOwner {
  public:
    explicit PerBufferMap(std::mutex* guard = nullptr) : guard_(guard) {
    }

    PerBufferMap(const PerBufferMap&)            = delete;
    PerBufferMap& operator=(const PerBufferMap&) = delete;

    ~PerBufferMap() {
        Clear();
    }

    // buffer's entry, or nullptr.
    [[nodiscard]] T* Find(const Buffer& buffer) {
        const auto it = entries_.find(&buffer);
        if (it == entries_.end()) {
            return nullptr;
        }
        if (it->second.instanceId != buffer.InstanceId()) {
            buffer.DetachEntryOwner(*this);
            entries_.erase(it);
            return nullptr;
        }
        return &it->second.value;
    }

    T& Set(const Buffer& buffer, T value) {
        const auto [it, inserted] = entries_.insert_or_assign(&buffer, Entry{buffer.InstanceId(), std::move(value)});
        if (inserted) {
            buffer.AttachEntryOwner(*this);
        }
        return it->second.value;
    }

    // Stores value only when buffer has no entry.
    T& SetIfAbsent(const Buffer& buffer, T value) {
        if (T* existing = Find(buffer)) {
            return *existing;
        }
        return Set(buffer, std::move(value));
    }

    void Erase(const Buffer& buffer) {
        if (entries_.erase(&buffer) > 0) {
            buffer.DetachEntryOwner(*this);
        }
    }

    void Clear() {
        for (const auto& [buffer, entry] : entries_) {
            buffer->DetachEntryOwner(*this);
        }
        entries_.clear();
    }

    [[nodiscard]] std::size_t Size() const {
        return entries_.size();
    }

    void ForgetBuffer(const Buffer* buffer) override {
        if (guard_ != nullptr) {
            const std::lock_guard lock(*guard_);
            entries_.erase(buffer);
            return;
        }
        entries_.erase(buffer);
    }

  private:
    struct Entry {
        std::size_t instanceId = 0;
        T           value;
    };

    std::unordered_map<const Buffer*, Entry> entries_;
    std::mutex*                              guard_;
};

} // namespace ned::text

#endif // NED_TEXT_PER_BUFFER_MAP_H
