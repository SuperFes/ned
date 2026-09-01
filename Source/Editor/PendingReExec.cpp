#include "PendingReExec.h"

#include <mutex>

namespace ned::editor {

namespace {

    std::mutex& PendingReExecMutex() {
        static std::mutex mutex;
        return mutex;
    }

    std::optional<PendingReExecRequest>& PendingReExecStorage() {
        static std::optional<PendingReExecRequest> request;
        return request;
    }

} // namespace

void SetPendingReExec(PendingReExecRequest request) {
    const std::lock_guard<std::mutex> lock(PendingReExecMutex());
    PendingReExecStorage() = std::move(request);
}

std::optional<PendingReExecRequest> TakePendingReExec() {
    const std::lock_guard<std::mutex>   lock(PendingReExecMutex());
    std::optional<PendingReExecRequest> result = std::move(PendingReExecStorage());
    PendingReExecStorage().reset();
    return result;
}

void ResetPendingReExecForTesting() {
    const std::lock_guard<std::mutex> lock(PendingReExecMutex());
    PendingReExecStorage().reset();
}

} // namespace ned::editor
