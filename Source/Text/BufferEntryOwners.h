//
// How a Buffer tells the maps holding state keyed by its address that it is
// gone, so no such entry can outlive the buffer (see PerBufferMap.h).
//

#ifndef NED_TEXT_BUFFER_ENTRY_OWNERS_H
#define NED_TEXT_BUFFER_ENTRY_OWNERS_H

#include <utility>
#include <vector>

namespace ned::text {

class Buffer;

// Holds entries keyed by Buffer address. ForgetBuffer drops the entry for a
// buffer being destroyed; it must not call back into that buffer.
class BufferEntryOwner {
  public:
    virtual void ForgetBuffer(const Buffer* buffer) = 0;

  protected:
    ~BufferEntryOwner() = default;
};

// A Buffer's record of the owners holding an entry under its address. An
// entry belongs to the object at that address, not to its contents: copies
// and moves start with no record, and assigning over a buffer drops the
// entries stored for what was there.
class BufferEntryOwners {
  public:
    BufferEntryOwners() = default;
    BufferEntryOwners(const BufferEntryOwners& /*other*/) noexcept {
    }
    BufferEntryOwners(BufferEntryOwners&& /*other*/) noexcept {
    }
    BufferEntryOwners& operator=(const BufferEntryOwners& other) noexcept {
        if (this != &other) {
            ForgetAll();
        }
        return *this;
    }
    BufferEntryOwners& operator=(BufferEntryOwners&& other) noexcept {
        if (this != &other) {
            ForgetAll();
        }
        return *this;
    }

    ~BufferEntryOwners() {
        ForgetAll();
    }

    void Add(BufferEntryOwner& owner, const Buffer* key) {
        owners_.emplace_back(&owner, key);
    }

    void Remove(const BufferEntryOwner& owner) {
        std::erase_if(owners_, [&owner](const auto& registered) { return registered.first == &owner; });
    }

  private:
    void ForgetAll() {
        for (const auto& [owner, key] : std::exchange(owners_, {})) {
            owner->ForgetBuffer(key);
        }
    }

    std::vector<std::pair<BufferEntryOwner*, const Buffer*>> owners_;
};

} // namespace ned::text

#endif // NED_TEXT_BUFFER_ENTRY_OWNERS_H
