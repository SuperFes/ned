#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>

#include "Text/Buffer.h"
#include "Text/PerBufferMap.h"

using ned::text::Buffer;
using ned::text::PerBufferMap;

TEST_CASE("PerBufferMap finds what was set for a buffer and nothing for another", "[PerBufferMap]") {
    PerBufferMap<std::string> map;
    Buffer                    a("a");
    Buffer                    b("b");
    map.Set(a, "for a");

    REQUIRE(map.Find(a) != nullptr);
    REQUIRE(*map.Find(a) == "for a");
    REQUIRE(map.Find(b) == nullptr);

    map.Erase(a);
    REQUIRE(map.Find(a) == nullptr);
}

TEST_CASE("PerBufferMap never hands a freed buffer's entry to a new buffer at the same address", "[PerBufferMap]") {
    PerBufferMap<std::string> map;
    std::optional<Buffer>     slot;

    slot.emplace("first");
    const Buffer* address = &*slot;
    map.Set(*slot, "first's entry");
    slot.reset();

    slot.emplace("second");
    REQUIRE(&*slot == address);
    REQUIRE(map.Find(*slot) == nullptr);
    REQUIRE(map.Size() == 0);
}

TEST_CASE("PerBufferMap::SetIfAbsent keeps a live entry but replaces a stale one", "[PerBufferMap]") {
    PerBufferMap<std::string> map;
    std::optional<Buffer>     slot;

    slot.emplace("first");
    map.Set(*slot, "kept");
    REQUIRE(map.SetIfAbsent(*slot, "ignored") == "kept");
    slot.reset();

    slot.emplace("second");
    REQUIRE(map.SetIfAbsent(*slot, "fresh") == "fresh");
}
