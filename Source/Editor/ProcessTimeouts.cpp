#include "ProcessTimeouts.h"

#include <algorithm>
#include <mutex>

namespace ned::editor {

namespace {

    std::mutex& TimeoutsMutex() {
        static std::mutex mutex;
        return mutex;
    }

    int& SubprocessReadTimeoutMsStorage() {
        static int milliseconds = 5000;
        return milliseconds;
    }

    int& SubprocessWriteTimeoutMsStorage() {
        static int milliseconds = 5000;
        return milliseconds;
    }

    int& ProtocolStallTimeoutMsStorage() {
        static int milliseconds = 30000;
        return milliseconds;
    }

    int& ProtocolRequestTimeoutMsStorage() {
        static int milliseconds = 30000;
        return milliseconds;
    }

} // namespace

void SetSubprocessReadTimeoutMs(int milliseconds) {
    const std::lock_guard<std::mutex> lock(TimeoutsMutex());
    SubprocessReadTimeoutMsStorage() = std::max(1, milliseconds);
}

std::chrono::milliseconds SubprocessReadTimeoutMs() {
    const std::lock_guard<std::mutex> lock(TimeoutsMutex());
    return std::chrono::milliseconds(SubprocessReadTimeoutMsStorage());
}

void SetSubprocessWriteTimeoutMs(int milliseconds) {
    const std::lock_guard<std::mutex> lock(TimeoutsMutex());
    SubprocessWriteTimeoutMsStorage() = std::max(1, milliseconds);
}

std::chrono::milliseconds SubprocessWriteTimeoutMs() {
    const std::lock_guard<std::mutex> lock(TimeoutsMutex());
    return std::chrono::milliseconds(SubprocessWriteTimeoutMsStorage());
}

void SetProtocolStallTimeoutMs(int milliseconds) {
    const std::lock_guard<std::mutex> lock(TimeoutsMutex());
    ProtocolStallTimeoutMsStorage() = std::max(1, milliseconds);
}

std::chrono::milliseconds ProtocolStallTimeoutMs() {
    const std::lock_guard<std::mutex> lock(TimeoutsMutex());
    return std::chrono::milliseconds(ProtocolStallTimeoutMsStorage());
}

void SetProtocolRequestTimeoutMs(int milliseconds) {
    const std::lock_guard<std::mutex> lock(TimeoutsMutex());
    ProtocolRequestTimeoutMsStorage() = std::max(1, milliseconds);
}

std::chrono::milliseconds ProtocolRequestTimeoutMs() {
    const std::lock_guard<std::mutex> lock(TimeoutsMutex());
    return std::chrono::milliseconds(ProtocolRequestTimeoutMsStorage());
}

} // namespace ned::editor
