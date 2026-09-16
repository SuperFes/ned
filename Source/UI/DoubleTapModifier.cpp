#include "UI/DoubleTapModifier.h"

namespace ned::ui {

DoubleTapModifierDetector::DoubleTapModifierDetector(std::chrono::milliseconds window) : window_(window) {
}

bool DoubleTapModifierDetector::Feed(const ncinput& input) {
    if (input.id != NCKEY_LSHIFT && input.id != NCKEY_RSHIFT) {
        Reset();
        return false;
    }
    if (input.evtype != NCTYPE_PRESS) {
        return false; // this Shift key's own repeat/release -- inert
    }

    const auto now = std::chrono::steady_clock::now();
    if (pendingSince_ && (now - *pendingSince_) <= window_) {
        pendingSince_.reset();
        return true;
    }
    pendingSince_ = now;
    return false;
}

void DoubleTapModifierDetector::Reset() {
    pendingSince_.reset();
}

} // namespace ned::ui
