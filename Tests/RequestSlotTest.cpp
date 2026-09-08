#include <catch2/catch_test_macros.hpp>

#include "UI/BufferView/RequestSlot.h"

using ned::ui::bufferview::RequestSlot;

TEST_CASE("A freshly issued token is current", "[RequestSlot]") {
    RequestSlot slot;

    const RequestSlot::Token token = slot.Begin();

    REQUIRE_FALSE(slot.IsStale(token));
}

TEST_CASE("Issuing again supersedes the token in flight", "[RequestSlot]") {
    RequestSlot slot;

    const RequestSlot::Token first  = slot.Begin();
    const RequestSlot::Token second = slot.Begin();

    // The reply to the first request may still land; it must be dropped.
    REQUIRE(slot.IsStale(first));
    REQUIRE_FALSE(slot.IsStale(second));
}

TEST_CASE("Cancel stales the token in flight without issuing a replacement", "[RequestSlot]") {
    RequestSlot slot;

    const RequestSlot::Token token = slot.Begin();
    slot.Cancel();

    REQUIRE(slot.IsStale(token));
}

TEST_CASE("A default-initialised token reads as stale, even on a fresh slot", "[RequestSlot]") {
    RequestSlot slot;

    // Token{} is 0 and no slot ever hands 0 out, so a callback holding one is
    // dropped rather than matching a slot that has issued nothing yet.
    REQUIRE(slot.IsStale(RequestSlot::Token{}));
    slot.Begin();
    REQUIRE(slot.IsStale(RequestSlot::Token{}));
}

TEST_CASE("Current is usable before anything has been issued", "[RequestSlot]") {
    RequestSlot slot;

    // A request that rides the current generation rather than superseding it
    // must still be considered current when it is the first thing to go out.
    const RequestSlot::Token token = slot.Current();

    REQUIRE_FALSE(slot.IsStale(token));
}

TEST_CASE("Current reports the token in flight without superseding it", "[RequestSlot]") {
    RequestSlot slot;

    const RequestSlot::Token issued  = slot.Begin();
    const RequestSlot::Token current = slot.Current();

    REQUIRE(current == issued);
    REQUIRE_FALSE(slot.IsStale(issued));
}

TEST_CASE("Slots are independent of one another", "[RequestSlot]") {
    RequestSlot hover;
    RequestSlot completion;

    const RequestSlot::Token hoverToken = hover.Begin();
    completion.Begin();
    completion.Begin();

    // A busy neighbour must not stale this slot's own in-flight request.
    REQUIRE_FALSE(hover.IsStale(hoverToken));
}
