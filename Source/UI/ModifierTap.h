//
// copilot-key follow-up: detects a deliberate *tap* of the Super key --
// pressed and released with nothing else in between -- and reports it as an
// ordinary KeyChord so the keymap can bind it like any other key.
// DoubleTapModifier.h's exact shape and for the same reason: pure,
// buffer-free, fed one raw event at a time by the caller, because
// KeyTranslation.cpp drops a bare modifier press (IsBareModifierKey) before
// it can ever become a KeyChord.
//
// "Super" here means the whole Super/Hyper/Meta family on both sides, which
// Notcurses reports as six distinct ids -- which one a terminal picks for the
// physical Windows key is not knowable in advance (KDE calls it Meta, X11
// usually maps it to Super). Alt is excluded: it is ned's own "Meta", and
// Shift+Alt is a live layout-switch chord on many desktops.
//
// This exists because some keyboards' "Copilot" key is a modifier-only
// chord. Microsoft's reference implementation sends Left Shift + Left Win +
// F23 (which ned already sees as an ordinary Shift+F11, since terminfo has
// always meant shifted F11 by F23), but OEMs vary and at least one laptop
// emits a bare Shift+Super with no third key at all. A modifier-only chord
// has no base key for a keymap entry to name, so it cannot be a binding --
// `s-` notation would have nothing to attach to. A gesture is the only
// shape it can take.
//
// Order-independent, which is not a nicety: a human presses Shift and then
// Super, but the Copilot key on real hardware emits Super first and Shift
// second. A detector that checked for Shift at the moment Super went down
// matched the human gesture and missed the key it exists for. The gesture is
// armed by both halves being down together, in either order, and completed
// by whichever comes up first.
//
// A bare tap yields `SUPER`; one with Shift held yields `S-SUPER`. Neither
// means anything until a keymap binds it, which is the point: this reports
// what was pressed and has no opinion about what it does.
//
// Fires on RELEASE, not press. A press-fire would trigger every time
// Shift+Super was held as the prefix of some larger chord; waiting for the
// release with nothing in between is what makes this a deliberate tap
// rather than the start of something else. A held modifier's own repeat
// stream is inert for the same reason DoubleTapModifierDetector's is: it
// must not cascade into repeated fires.
//
// Whether this can fire at all is terminal-dependent and cannot be checked
// in advance -- bare modifier presses are only reported under the Kitty
// keyboard protocol, which Notcurses negotiates on its own with no
// capability query this codebase can consult (see
// SearchEverywhereGestureSettings.h, which carries the same caveat for the
// double-tap gesture). A desktop that grabs Super for itself -- KDE's own
// launcher does for a bare tap -- takes it before the terminal ever sees
// it, in which case nothing here ever runs. No setting guards that: what
// this produces is an ordinary chord, so the escape hatch is the keymap
// itself, exactly as it is for every other binding.
//

#ifndef NED_UI_MODIFIERTAP_H
#define NED_UI_MODIFIERTAP_H

#include <optional>

#include <notcurses/notcurses.h>

#include "Editor/Key.h"

namespace ned::ui {

class ModifierTapDetector {
  public:
    // Returns a chord exactly on the event that completes a tap, and
    // std::nullopt for everything else. The chord is
    // SpecialKey::Super plus whichever of Shift/Control/Alt were held for
    // the whole gesture -- so the Copilot key is `S-SUPER`, a bare tap is
    // `SUPER`, and a keymap decides what any of them mean. Nothing here
    // knows about the ACP panel or any other command.
    //
    // Unlike the double-tap detector this needs no time window: the gesture
    // is bounded by the key's own press/release pair rather than by two
    // independent events that have to be judged "close enough".
    [[nodiscard]] std::optional<editor::KeyChord> Feed(const ncinput& input);

    // A mouse action counts as "something else happened", the same way it
    // does for the double-tap gesture.
    void Reset();

  private:
    // Physical state of each half, tracked from their own press/release
    // events (and from the modifier bits as a second source), plus whether
    // the two have been down together at any point during this gesture.
    bool shiftDown_ = false;
    bool superDown_ = false;
    bool armed_     = false;
    // Which non-Super modifiers were down when the gesture armed. Captured
    // at arming rather than read at release, so letting go of Shift a
    // moment before Super still yields `S-SUPER` rather than a bare
    // `SUPER` -- on real hardware the two are released together and the
    // order they arrive in is not something to depend on.
    bool armedWithShift_   = false;
    bool armedWithControl_ = false;
    bool armedWithAlt_     = false;
};

} // namespace ned::ui

#endif // NED_UI_MODIFIERTAP_H
