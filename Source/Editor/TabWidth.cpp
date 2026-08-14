#include "TabWidth.h"

#include <algorithm>
#include <mutex>

namespace ned::editor {

namespace {

    std::mutex& WidthMutex() {
        static std::mutex mutex;
        return mutex;
    }

    int& WidthStorage() {
        static int width = 4;
        return width;
    }

} // namespace

void SetTabWidth(int columns) {
    const std::lock_guard<std::mutex> lock(WidthMutex());
    WidthStorage() = std::max(1, columns); // non-positive would hang/underflow the expansion loop
}

int TabWidth() {
    const std::lock_guard<std::mutex> lock(WidthMutex());
    return WidthStorage();
}

} // namespace ned::editor
