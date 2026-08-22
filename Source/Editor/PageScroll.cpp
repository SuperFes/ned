#include "PageScroll.h"

#include <algorithm>
#include <mutex>

namespace ned::editor {

namespace {

    std::mutex& FractionMutex() {
        static std::mutex mutex;
        return mutex;
    }

    double& FractionStorage() {
        static double fraction = 0.65;
        return fraction;
    }

} // namespace

void SetPageScrollFraction(double fraction) {
    const std::lock_guard<std::mutex> lock(FractionMutex());
    FractionStorage() = std::clamp(fraction, 0.01, 1.0);
}

double PageScrollFraction() {
    const std::lock_guard<std::mutex> lock(FractionMutex());
    return FractionStorage();
}

} // namespace ned::editor
