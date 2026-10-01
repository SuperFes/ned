//
// Which buffer something was computed for or sent about, still comparable
// after that buffer is gone. A bare Buffer* is not: a buffer allocated where
// a closed one used to be has the same address, so a stale pointer compares
// equal to an unrelated live buffer. The address is kept only for comparing
// and is never dereferenced.
//

#ifndef NED_TEXT_BUFFERIDENTITY_H
#define NED_TEXT_BUFFERIDENTITY_H

#include <cstddef>

namespace ned::text {

class Buffer;

class BufferIdentity {
  public:
    // Names no buffer, and is never Is() any buffer.
    BufferIdentity() = default;

    explicit BufferIdentity(const Buffer& buffer);

    [[nodiscard]] bool Is(const Buffer& buffer) const;

    [[nodiscard]] bool Empty() const {
        return address_ == nullptr;
    }

    // Empty identities compare equal to each other.
    [[nodiscard]] bool operator==(const BufferIdentity&) const = default;

  private:
    const Buffer* address_    = nullptr;
    std::size_t   instanceId_ = 0;
};

} // namespace ned::text

#endif // NED_TEXT_BUFFERIDENTITY_H
