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
