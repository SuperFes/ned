#include "PointerShapeSettings.h"

#include <mutex>

namespace ned::editor {

namespace {

    std::mutex g_mutex;
    bool       g_enabled = true;

} // namespace

void SetPointerShapesEnabled(bool enabled) {
    const std::lock_guard lock(g_mutex);
    g_enabled = enabled;
}

bool PointerShapesEnabled() {
    const std::lock_guard lock(g_mutex);
    return g_enabled;
}

} // namespace ned::editor
