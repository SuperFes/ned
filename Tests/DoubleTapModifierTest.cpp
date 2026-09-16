#include <catch2/catch_test_macros.hpp>

#include <thread>

#include "UI/DoubleTapModifier.h"

using ned::ui::DoubleTapModifierDetector;

namespace {

ncinput Press(std::uint32_t id) {
    ncinput input{};
    input.id     = id;
    input.evtype = NCTYPE_PRESS;
    return input;
}

ncinput Repeat(std::uint32_t id) {
    ncinput input{};
    input.id     = id;
    input.evtype = NCTYPE_REPEAT;
    return input;
}

ncinput Release(std::uint32_t id) {
    ncinput input{};
    input.id     = id;
    input.evtype = NCTYPE_RELEASE;
    return input;
}

} // namespace

TEST_CASE("Two Shift presses close together complete a double-tap", "[DoubleTapModifier]") {
    DoubleTapModifierDetector detector;
    REQUIRE_FALSE(detector.Feed(Press(NCKEY_LSHIFT)));
    REQUIRE(detector.Feed(Press(NCKEY_LSHIFT)));
}

TEST_CASE("Either Shift key counts -- Left then Right completes a double-tap", "[DoubleTapModifier]") {
    DoubleTapModifierDetector detector;
    REQUIRE_FALSE(detector.Feed(Press(NCKEY_LSHIFT)));
    REQUIRE(detector.Feed(Press(NCKEY_RSHIFT)));
}

TEST_CASE("A second press past the window does not fire, and becomes the new pending press",
          "[DoubleTapModifier]") {
    DoubleTapModifierDetector detector(std::chrono::milliseconds(50));
    REQUIRE_FALSE(detector.Feed(Press(NCKEY_LSHIFT)));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    REQUIRE_FALSE(detector.Feed(Press(NCKEY_LSHIFT))); // too late to pair with the first
    REQUIRE(detector.Feed(Press(NCKEY_LSHIFT)));       // but immediately pairs with itself
}

TEST_CASE("A held Shift's repeat stream never fires and never resets a pending press",
          "[DoubleTapModifier]") {
    DoubleTapModifierDetector detector;
    REQUIRE_FALSE(detector.Feed(Press(NCKEY_LSHIFT)));
    REQUIRE_FALSE(detector.Feed(Repeat(NCKEY_LSHIFT)));
    REQUIRE_FALSE(detector.Feed(Repeat(NCKEY_LSHIFT)));
    REQUIRE(detector.Feed(Press(NCKEY_LSHIFT))); // the pending press from before the repeats still counts
}

TEST_CASE("A Shift release is inert -- doesn't fire and doesn't reset a pending press", "[DoubleTapModifier]") {
    DoubleTapModifierDetector detector;
    REQUIRE_FALSE(detector.Feed(Press(NCKEY_LSHIFT)));
    REQUIRE_FALSE(detector.Feed(Release(NCKEY_LSHIFT)));
    REQUIRE(detector.Feed(Press(NCKEY_LSHIFT)));
}

TEST_CASE("A real key press in between resets the pending tap", "[DoubleTapModifier]") {
    DoubleTapModifierDetector detector;
    REQUIRE_FALSE(detector.Feed(Press(NCKEY_LSHIFT)));
    REQUIRE_FALSE(detector.Feed(Press('a')));
    REQUIRE_FALSE(detector.Feed(Press(NCKEY_LSHIFT))); // fresh pending, not a completion
}

TEST_CASE("A different bare modifier in between resets the pending tap -- this is Shift-specific",
          "[DoubleTapModifier]") {
    // NCKEY_LCTRL falls inside KeyTranslation.cpp's own IsBareModifierKey
    // range (it covers all 14 modifier keys), but this detector must not
    // treat Shift-then-Ctrl as a double-tap of Shift.
    DoubleTapModifierDetector detector;
    REQUIRE_FALSE(detector.Feed(Press(NCKEY_LSHIFT)));
    REQUIRE_FALSE(detector.Feed(Press(NCKEY_LCTRL)));
    REQUIRE_FALSE(detector.Feed(Press(NCKEY_LSHIFT)));
}

TEST_CASE("Reset() clears a pending press", "[DoubleTapModifier]") {
    DoubleTapModifierDetector detector;
    REQUIRE_FALSE(detector.Feed(Press(NCKEY_LSHIFT)));
    detector.Reset();
    REQUIRE_FALSE(detector.Feed(Press(NCKEY_LSHIFT)));
}
