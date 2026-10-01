#include "BufferIdentity.h"

#include "Buffer.h"

namespace ned::text {

BufferIdentity::BufferIdentity(const Buffer& buffer) : address_(&buffer), instanceId_(buffer.InstanceId()) {
}

bool BufferIdentity::Is(const Buffer& buffer) const {
    return address_ == &buffer && instanceId_ == buffer.InstanceId();
}

} // namespace ned::text
