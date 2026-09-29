#include "Compose.h"

#include <cstddef>
#include <unordered_map>
#include <utility>

#include "Text/Buffer.h"

namespace ned::editor::acp {

namespace {
    // Keyed by InstanceId, not address: a buffer freed without passing
    // through the close funnel must not hand its callbacks to whatever is
    // allocated where it was.
    std::unordered_map<std::size_t, ComposeCallbacks>& Registry() {
        static std::unordered_map<std::size_t, ComposeCallbacks> registry;
        return registry;
    }
} // namespace

void AttachCompose(const text::Buffer& buffer, ComposeCallbacks callbacks) {
    Registry()[buffer.InstanceId()] = std::move(callbacks);
}

std::optional<ComposeCallbacks> DetachCompose(const text::Buffer& buffer) {
    const auto it = Registry().find(buffer.InstanceId());
    if (it == Registry().end()) {
        return std::nullopt;
    }
    ComposeCallbacks callbacks = std::move(it->second);
    Registry().erase(it);
    return callbacks;
}

} // namespace ned::editor::acp
