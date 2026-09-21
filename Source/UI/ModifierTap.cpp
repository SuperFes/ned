#include "UI/ModifierTap.h"

#include <cstdint>

namespace ned::ui {

namespace {

    bool IsShiftKey(std::uint32_t id) {
        return id == NCKEY_LSHIFT || id == NCKEY_RSHIFT;
    }

    // The whole Super/Hyper/Meta family, both sides. Notcurses gives these
    // six ids distinct values (nckeys.h, ordered per the Kitty protocol),
    // and which one a given terminal reports for the physical Windows key
    // is not something this codebase can know in advance -- KDE calls it
    // "Meta", X11 usually maps it to Super, and the protocol has a slot for
    // Hyper besides. Accepting the family rather than one hardcoded id is
    // what makes this work without guessing which.
    //
    // NCKEY_LALT/NCKEY_RALT are deliberately NOT here: Alt is ned's own
    // "Meta" (Editor/Key.h), and Shift+Alt is a live layout-switch chord on
    // many desktops -- treating it as this gesture would be a real false
    // trigger rather than a theoretical one.
    bool IsSuperKey(std::uint32_t id) {
        return id == NCKEY_LSUPER || id == NCKEY_RSUPER || id == NCKEY_LHYPER || id == NCKEY_RHYPER ||
               id == NCKEY_LMETA || id == NCKEY_RMETA;
    }

} // namespace

std::optional<editor::KeyChord> ModifierTapDetector::Feed(const ncinput& input) {
    const bool shiftKey = IsShiftKey(input.id);
    const bool superKey = IsSuperKey(input.id);

    // Anything that is not one of the two modifiers this gesture is made of
    // ends it -- Shift+Super+X is a chord, not a tap.
    if (!shiftKey && !superKey) {
        Reset();
        return std::nullopt;
    }
    if (input.evtype == NCTYPE_REPEAT) {
        return std::nullopt; // a held modifier's own repeat stream must not cascade into repeated fires
    }

    if (input.evtype == NCTYPE_PRESS) {
        if (shiftKey) {
            shiftDown_ = true;
        }
        if (superKey) {
            superDown_ = true;
        }
        // The modifier bits are a second source of evidence for the other
        // key already being held, for a terminal that reports the chord
        // without a separate event for each half.
        if ((input.modifiers & NCKEY_MOD_SHIFT) != 0) {
            shiftDown_ = true;
        }
        if ((input.modifiers & NCKEY_MOD_SUPER) != 0) {
            superDown_ = true;
        }
        // Super being down is what arms it; the other modifiers are
        // captured, not required, so a bare tap yields `SUPER` and a
        // shifted one `S-SUPER` and the keymap decides whether either
        // means anything.
        //
        // Re-evaluated on EVERY press of either key, which is the whole
        // point: order matters in practice and not the way you would
        // guess. A human presses Shift then Super, but the Copilot key on
        // real hardware emits Super first and Shift second -- so a Shift
        // press arriving after Super has to be able to upgrade an already
        // armed bare tap into a shifted one.
        if (superDown_) {
            armed_            = true;
            armedWithShift_   = shiftDown_;
            armedWithControl_ = (input.modifiers & NCKEY_MOD_CTRL) != 0;
            armedWithAlt_     = (input.modifiers & NCKEY_MOD_ALT) != 0;
        }
        return std::nullopt;
    }

    // Release. Whichever half comes up first completes the gesture, for the
    // same order-independence reason -- and disarms, so the second release
    // cannot fire it a second time.
    if (shiftKey) {
        shiftDown_ = false;
    }
    if (superKey) {
        superDown_ = false;
    }
    if (!armed_) {
        return std::nullopt;
    }
    armed_ = false;
    return editor::KeyChord{.Control = armedWithControl_,
                            .Meta    = armedWithAlt_,
                            .Shift   = armedWithShift_,
                            .Special = editor::SpecialKey::Super};
}

void ModifierTapDetector::Reset() {
    armed_ = false;
    // shiftDown_/superDown_ are deliberately NOT cleared: they track the
    // physical keys, which a mouse click or an unrelated keystroke does not
    // lift. Their own release events are what clear them.
}

} // namespace ned::ui
