//
// ModifierTapDetector (Source/UI/ShiftSuperTap.h) -- the modifier-only
// gesture some keyboards' "Copilot" key produces. DoubleTapModifierTest's
// own fixture shape: raw ncinput built by hand, since this detector sits
// earlier in the pipeline than KeyChord.
//

#include <catch2/catch_test_macros.hpp>

#include <optional>

#include "Editor/Key.h"
#include "UI/ModifierTap.h"

using ned::editor::KeyChord;
using ned::editor::SpecialKey;
using ned::ui::ModifierTapDetector;

namespace {

ncinput Event(std::uint32_t id, ncintype_e evtype, unsigned modifiers = 0) {
    ncinput input{};
    input.id        = id;
    input.evtype    = evtype;
    input.modifiers = modifiers;
    return input;
}

ncinput ShiftedPress(std::uint32_t id) {
    return Event(id, NCTYPE_PRESS, NCKEY_MOD_SHIFT);
}

// A human's Shift+Super: Shift goes down first.
[[nodiscard]] std::optional<KeyChord> FeedHumanShiftSuper(ModifierTapDetector& detector) {
    std::optional<KeyChord> fired;
    for (const ncinput& event : {Event(NCKEY_LSHIFT, NCTYPE_PRESS), ShiftedPress(NCKEY_LSUPER),
                                 Event(NCKEY_LSUPER, NCTYPE_RELEASE, NCKEY_MOD_SHIFT),
                                 Event(NCKEY_LSHIFT, NCTYPE_RELEASE)}) {
        if (const auto chord = detector.Feed(event)) {
            fired = chord;
        }
    }
    return fired;
}

// The REAL Copilot key, which emits Super FIRST and Shift second -- the
// inverse of how a human does it, and the shape that made an
// order-dependent detector miss the very key it was written for.
[[nodiscard]] std::optional<KeyChord> FeedCopilotKey(ModifierTapDetector& detector) {
    std::optional<KeyChord> fired;
    for (const ncinput& event : {Event(NCKEY_LSUPER, NCTYPE_PRESS), Event(NCKEY_LSHIFT, NCTYPE_PRESS, NCKEY_MOD_SUPER),
                                 Event(NCKEY_LSHIFT, NCTYPE_RELEASE, NCKEY_MOD_SUPER),
                                 Event(NCKEY_LSUPER, NCTYPE_RELEASE)}) {
        if (const auto chord = detector.Feed(event)) {
            fired = chord;
        }
    }
    return fired;
}

} // namespace

TEST_CASE("ModifierTapDetector reports a Shift+Super tap as the S-SUPER chord", "[ModifierTap]") {
    ModifierTapDetector detector;
    const auto          human = FeedHumanShiftSuper(detector);
    REQUIRE(human);
    REQUIRE(human->Special == SpecialKey::Super);
    REQUIRE(human->Shift);
    REQUIRE_FALSE(human->Control);
    REQUIRE_FALSE(human->Meta);
    REQUIRE(ned::editor::FormatKeyChord(*human) == "S-SUPER");

    // And it re-arms: the gesture is repeatable, which is the whole point
    // of a toggle.
    REQUIRE(FeedHumanShiftSuper(detector));
}

// The bug the real hardware found: the Copilot key emits Super BEFORE
// Shift, so a detector that only looked for Shift at the moment Super went
// down matched a human's gesture and missed the actual key.
TEST_CASE("ModifierTapDetector handles Super-then-Shift, the order the real key uses", "[ModifierTap]") {
    ModifierTapDetector detector;
    const auto          copilot = FeedCopilotKey(detector);
    REQUIRE(copilot);
    REQUIRE(copilot->Special == SpecialKey::Super);
    REQUIRE(copilot->Shift); // Shift arriving second still upgrades the chord
    REQUIRE(ned::editor::FormatKeyChord(*copilot) == "S-SUPER");
}

// A bare tap is a chord too -- it simply names nothing in the default
// keymap. Reporting it rather than swallowing it is what makes this a
// general mechanism instead of one hardcoded gesture.
TEST_CASE("ModifierTapDetector reports a bare Super tap as the SUPER chord", "[ModifierTap]") {
    ModifierTapDetector detector;
    REQUIRE_FALSE(detector.Feed(Event(NCKEY_LSUPER, NCTYPE_PRESS)));
    const auto bare = detector.Feed(Event(NCKEY_LSUPER, NCTYPE_RELEASE));
    REQUIRE(bare);
    REQUIRE(bare->Special == SpecialKey::Super);
    REQUIRE_FALSE(bare->Shift);
    REQUIRE(ned::editor::FormatKeyChord(*bare) == "SUPER");
}

TEST_CASE("ModifierTapDetector fires on the release, not the press", "[ModifierTap]") {
    ModifierTapDetector detector;
    REQUIRE_FALSE(detector.Feed(Event(NCKEY_LSHIFT, NCTYPE_PRESS)));
    // Holding it must do nothing -- a press-fire would trigger every time
    // Shift+Super was held as the prefix of some larger chord.
    REQUIRE_FALSE(detector.Feed(ShiftedPress(NCKEY_LSUPER)));
    REQUIRE(detector.Feed(Event(NCKEY_LSUPER, NCTYPE_RELEASE, NCKEY_MOD_SHIFT)));
}

TEST_CASE("ModifierTapDetector is inert across a held Super's repeat stream", "[ModifierTap]") {
    ModifierTapDetector detector;
    (void)detector.Feed(Event(NCKEY_LSHIFT, NCTYPE_PRESS)); // setup: Shift goes down first
    REQUIRE_FALSE(detector.Feed(ShiftedPress(NCKEY_LSUPER)));
    for (int i = 0; i < 5; ++i) {
        REQUIRE_FALSE(detector.Feed(Event(NCKEY_LSUPER, NCTYPE_REPEAT, NCKEY_MOD_SHIFT)));
    }
    // One fire at the end, not one per repeat.
    REQUIRE(detector.Feed(Event(NCKEY_LSUPER, NCTYPE_RELEASE, NCKEY_MOD_SHIFT)));
}

TEST_CASE("ModifierTapDetector is cancelled by a real key in between", "[ModifierTap]") {
    ModifierTapDetector detector;
    (void)detector.Feed(Event(NCKEY_LSHIFT, NCTYPE_PRESS)); // setup: Shift goes down first
    REQUIRE_FALSE(detector.Feed(ShiftedPress(NCKEY_LSUPER)));
    // Shift+Super+X is a chord, not a tap -- the X ends it.
    REQUIRE_FALSE(detector.Feed(Event('x', NCTYPE_PRESS, NCKEY_MOD_SHIFT | NCKEY_MOD_SUPER)));
    REQUIRE_FALSE(detector.Feed(Event(NCKEY_LSUPER, NCTYPE_RELEASE, NCKEY_MOD_SHIFT)));
}

TEST_CASE("ModifierTapDetector is cancelled by a different modifier in between", "[ModifierTap]") {
    ModifierTapDetector detector;
    (void)detector.Feed(Event(NCKEY_LSHIFT, NCTYPE_PRESS)); // setup: Shift goes down first
    REQUIRE_FALSE(detector.Feed(ShiftedPress(NCKEY_LSUPER)));
    REQUIRE_FALSE(detector.Feed(Event(NCKEY_LCTRL, NCTYPE_PRESS, NCKEY_MOD_SHIFT)));
    REQUIRE_FALSE(detector.Feed(Event(NCKEY_LSUPER, NCTYPE_RELEASE, NCKEY_MOD_SHIFT)));
}

TEST_CASE("ModifierTapDetector treats Shift's own events as part of the gesture", "[ModifierTap]") {
    ModifierTapDetector detector;
    // Shift is held across the whole thing, so its own press/release must
    // not count as "something else happened" -- which is what distinguishes
    // this from the double-tap detector's own reset rule.
    REQUIRE_FALSE(detector.Feed(Event(NCKEY_RSHIFT, NCTYPE_PRESS)));
    REQUIRE_FALSE(detector.Feed(ShiftedPress(NCKEY_LSUPER)));
    REQUIRE_FALSE(detector.Feed(Event(NCKEY_RSHIFT, NCTYPE_REPEAT)));
    REQUIRE(detector.Feed(Event(NCKEY_LSUPER, NCTYPE_RELEASE, NCKEY_MOD_SHIFT)));
}

TEST_CASE("ModifierTapDetector Reset abandons a pending gesture", "[ModifierTap]") {
    ModifierTapDetector detector;
    (void)detector.Feed(Event(NCKEY_LSHIFT, NCTYPE_PRESS)); // setup: Shift goes down first
    REQUIRE_FALSE(detector.Feed(ShiftedPress(NCKEY_LSUPER)));
    detector.Reset(); // a mouse action, say
    REQUIRE_FALSE(detector.Feed(Event(NCKEY_LSUPER, NCTYPE_RELEASE, NCKEY_MOD_SHIFT)));
}

// The physical Windows key is reported as one of six distinct ids depending
// on the terminal (KDE calls it "Meta", X11 usually maps it to Super, and
// the Kitty protocol has a Hyper slot besides). Hardcoding one of them was
// the original bug: every other id took the "a real key ended it" path and
// reset the gesture instead.
TEST_CASE("ModifierTapDetector accepts the whole Super/Hyper/Meta family", "[ModifierTap]") {
    for (const std::uint32_t id : {NCKEY_LSUPER, NCKEY_RSUPER, NCKEY_LHYPER, NCKEY_RHYPER, NCKEY_LMETA, NCKEY_RMETA}) {
        ModifierTapDetector detector;
        INFO("modifier id " << id);
        (void)detector.Feed(Event(NCKEY_LSHIFT, NCTYPE_PRESS));
        REQUIRE_FALSE(detector.Feed(ShiftedPress(id)));
        const auto chord = detector.Feed(Event(id, NCTYPE_RELEASE, NCKEY_MOD_SHIFT));
        REQUIRE(chord);
        REQUIRE(chord->Special == SpecialKey::Super);
    }
}

// Alt must stay out of that family: it is ned's own "Meta" (Editor/Key.h),
// and Shift+Alt is a live layout-switch chord on many desktops -- treating
// it as this gesture would be a real false trigger.
TEST_CASE("ModifierTapDetector never fires for Shift+Alt", "[ModifierTap]") {
    for (const std::uint32_t id : {NCKEY_LALT, NCKEY_RALT}) {
        ModifierTapDetector detector;
        INFO("modifier id " << id);
        (void)detector.Feed(Event(NCKEY_LSHIFT, NCTYPE_PRESS));
        REQUIRE_FALSE(detector.Feed(ShiftedPress(id)));
        REQUIRE_FALSE(detector.Feed(Event(id, NCTYPE_RELEASE, NCKEY_MOD_SHIFT)));
    }
}

// Whether a terminal sets NCKEY_MOD_SHIFT on the Super press itself is
// exactly the kind of per-terminal detail that can't be assumed, so Shift's
// own press/release is tracked as a second source of evidence.
TEST_CASE("ModifierTapDetector fires when Shift is held but the modifier bit is absent", "[ModifierTap]") {
    ModifierTapDetector detector;
    (void)detector.Feed(Event(NCKEY_LSHIFT, NCTYPE_PRESS));
    REQUIRE_FALSE(detector.Feed(Event(NCKEY_LSUPER, NCTYPE_PRESS))); // no MOD_SHIFT reported
    REQUIRE(detector.Feed(Event(NCKEY_LSUPER, NCTYPE_RELEASE)));
}

TEST_CASE("ModifierTapDetector drops the Shift once it has been released", "[ModifierTap]") {
    ModifierTapDetector detector;
    (void)detector.Feed(Event(NCKEY_LSHIFT, NCTYPE_PRESS));
    (void)detector.Feed(Event(NCKEY_LSHIFT, NCTYPE_RELEASE));
    REQUIRE_FALSE(detector.Feed(Event(NCKEY_LSUPER, NCTYPE_PRESS)));
    const auto bare = detector.Feed(Event(NCKEY_LSUPER, NCTYPE_RELEASE));
    REQUIRE(bare);
    REQUIRE_FALSE(bare->Shift); // a plain SUPER, not S-SUPER
}
