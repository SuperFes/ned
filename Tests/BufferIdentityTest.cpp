#include <catch2/catch_test_macros.hpp>

#include <optional>

#include "Text/Buffer.h"
#include "Text/BufferIdentity.h"

using ned::text::Buffer;
using ned::text::BufferIdentity;

TEST_CASE("An identity is its own buffer and no other", "[BufferIdentity]") {
    Buffer first("first");
    Buffer second("second");

    const BufferIdentity identity(first);

    REQUIRE(identity.Is(first));
    REQUIRE_FALSE(identity.Is(second));
}

TEST_CASE("An empty identity is no buffer", "[BufferIdentity]") {
    Buffer buffer("scratch");

    REQUIRE(BufferIdentity().Empty());
    REQUIRE_FALSE(BufferIdentity().Is(buffer));
}

TEST_CASE("An identity is not a later buffer at the same address", "[BufferIdentity]") {
    std::optional<Buffer> slot;
    slot.emplace("closed");
    const BufferIdentity identity(*slot);
    const Buffer* const  address = &*slot;

    slot.emplace("opened");
    REQUIRE(&*slot == address);

    REQUIRE_FALSE(identity.Is(*slot));
    REQUIRE_FALSE(identity == BufferIdentity(*slot));
}
