#include "SearchSettings.h"

#include <algorithm>
#include <mutex>

namespace ned::editor {

namespace {

    std::mutex& ThreadsMutex() {
        static std::mutex mutex;
        return mutex;
    }

    int& ThreadsStorage() {
        static int threads = 4;
        return threads;
    }

} // namespace

void SetProjectSearchThreads(int threads) {
    const std::lock_guard<std::mutex> lock(ThreadsMutex());
    ThreadsStorage() = std::max(1, threads); // non-positive would leave nothing to run the scan at all
}

int ProjectSearchThreads() {
    const std::lock_guard<std::mutex> lock(ThreadsMutex());
    return ThreadsStorage();
}

} // namespace ned::editor
