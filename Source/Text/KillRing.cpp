#include "KillRing.h"

#include <utility>

namespace ned::text {

namespace {

    std::string JoinPieces(const std::vector<std::string>& pieces) {
        std::string joined;
        for (std::size_t i = 0; i < pieces.size(); ++i) {
            if (i > 0) {
                joined += '\n';
            }
            joined += pieces[i];
        }
        return joined;
    }

} // namespace

KillRing::KillRing(std::size_t capacity) : capacity_(capacity) {
}

void KillRing::Kill(std::string text) {
    KillPieces({std::move(text)});
}

void KillRing::KillPieces(std::vector<std::string> pieces) {
    if (pieces.empty()) {
        pieces.emplace_back(); // defensive; every real caller passes at least one piece
    }
    std::string joined = JoinPieces(pieces);
    ring_.push_front(Entry{std::move(pieces), std::move(joined)});

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
    return ring_[yankIndex_].joined;
}

const std::vector<std::string>& KillRing::CurrentPieces() const {
    static const std::vector<std::string> kEmpty;
    if (ring_.empty()) {
        return kEmpty;
    }
    return ring_[yankIndex_].pieces;
}

const std::string& KillRing::YankPop() {
    if (ring_.empty()) {
        return Current();
    }
    yankIndex_ = (yankIndex_ + 1) % ring_.size();
    return Current();
}

} // namespace ned::text
