#include <catch2/catch_test_macros.hpp>

#include "Text/Buffer.h"
#include "UI/ActiveBuffer.h"

using ned::text::Buffer;
using ned::ui::ActiveBuffer;

TEST_CASE("ActiveBuffer starts out pointing at the buffer it was constructed with", "[ActiveBuffer]") {
    Buffer       buffer("scratch");
    ActiveBuffer activeBuffer(buffer);

    REQUIRE(&activeBuffer.Get() == &buffer);
}

TEST_CASE("Set rebinds which buffer Get() returns", "[ActiveBuffer]") {
    Buffer       first("first");
    Buffer       second("second");
    ActiveBuffer activeBuffer(first);

    activeBuffer.Set(second);

    REQUIRE(&activeBuffer.Get() == &second);
    REQUIRE(&activeBuffer.Get() != &first);
}

TEST_CASE("The on-change hook fires with the new buffer only when Set actually changes it", "[ActiveBuffer]") {
    Buffer       first("first");
    Buffer       second("second");
    ActiveBuffer activeBuffer(first);

    Buffer* observed = nullptr;
    int     fires    = 0;
    activeBuffer.SetOnChange([&](Buffer& current) {
        observed = &current;
        ++fires;
    });

    activeBuffer.Set(first); // same buffer -- not a change
    REQUIRE(fires == 0);

    activeBuffer.Set(second);
    REQUIRE(fires == 1);
    REQUIRE(observed == &second);
}
