#include "HugeStructuralWindow.h"

#include <mutex>

namespace ned::editor {

namespace {

    constexpr std::size_t kDefaultHugeStructuralWindowBytes = 4 * 1024 * 1024;

    std::mutex& HugeStructuralWindowMutex() {
        static std::mutex mutex;
        return mutex;
    }

    std::size_t& HugeStructuralWindowBytesStorage() {
        static std::size_t bytes = kDefaultHugeStructuralWindowBytes;
        return bytes;
    }

} // namespace

void SetHugeStructuralWindowBytes(std::size_t bytes) {
    const std::lock_guard<std::mutex> lock(HugeStructuralWindowMutex());
    HugeStructuralWindowBytesStorage() = bytes;
}

std::size_t HugeStructuralWindowBytes() {
    const std::lock_guard<std::mutex> lock(HugeStructuralWindowMutex());
    return HugeStructuralWindowBytesStorage();
}

} // namespace ned::editor
