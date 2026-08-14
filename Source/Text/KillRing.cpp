#include "KillRing.h"

#include <utility>

namespace ned::text {

KillRing::KillRing(std::size_t capacity) : capacity_(capacity) {}

void KillRing::Kill(std::string text) {
    ring_.push_front(std::move(text));

    while (ring_.size() > capacity_) {
        ring_.pop_back();
    }

    yankIndex_ = 0;
}

bool KillRing::Empty() const {
    return ring_.empty();
}

const std::string& KillRing::Current() const {
    static const std::string kEmpty;
    if (ring_.empty()) {
        return kEmpty;
    }
    return ring_[yankIndex_];
}

const std::string& KillRing::YankPop() {
    if (ring_.empty()) {
        return Current();
    }
    yankIndex_ = (yankIndex_ + 1) % ring_.size();
    return Current();
}

} // namespace ned::text
