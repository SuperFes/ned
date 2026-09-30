#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

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

TEST_CASE("PerBufferMap drops a buffer's entry the moment the buffer is destroyed", "[PerBufferMap]") {
    PerBufferMap<std::string> map;
    Buffer                    kept("kept");
    map.Set(kept, "kept's entry");
    {
        Buffer temporary("temporary");
        map.Set(temporary, "temporary's entry");
        REQUIRE(map.Size() == 2);
    }
    REQUIRE(map.Size() == 1);
    REQUIRE(*map.Find(kept) == "kept's entry");
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
}

TEST_CASE("PerBufferMap forgets a buffer in every map holding it", "[PerBufferMap]") {
    PerBufferMap<std::string> names;
    PerBufferMap<int>         counts;
    {
        Buffer buffer("both");
        names.Set(buffer, "name");
        counts.Set(buffer, 1);
    }
    REQUIRE(names.Size() == 0);
    REQUIRE(counts.Size() == 0);
}

TEST_CASE("PerBufferMap destroyed before its buffers leaves them safe to destroy", "[PerBufferMap]") {
    Buffer                    buffer("outlives the map");
    PerBufferMap<std::string> survivor;
    survivor.Set(buffer, "survivor");
    {
        PerBufferMap<std::string> shortLived;
        shortLived.Set(buffer, "short-lived");
    }
    // The buffer's destruction must only reach `survivor`; ASan flags a
    // call into the destroyed map.
    REQUIRE(*survivor.Find(buffer) == "survivor");
}

TEST_CASE("PerBufferMap erase and clear stop the buffer from reaching the map", "[PerBufferMap]") {
    PerBufferMap<std::string> map;
    {
        Buffer erased("erased");
        Buffer cleared("cleared");
        map.Set(erased, "x");
        map.Erase(erased);
        map.Set(cleared, "y");
        map.Clear();
        map.Set(erased, "again");
        REQUIRE(map.Size() == 1);
    }
    REQUIRE(map.Size() == 0);
}

TEST_CASE("PerBufferMap keys a moved buffer's entry to the object at the original address", "[PerBufferMap]") {
    PerBufferMap<std::string> map;
    auto                      source = std::make_unique<Buffer>("source");
    map.Set(*source, "source's entry");

    Buffer moved(std::move(*source));
    REQUIRE(map.Find(moved) == nullptr);
    REQUIRE(map.Size() == 1);

    source.reset();
    REQUIRE(map.Size() == 0);
}

TEST_CASE("PerBufferMap drops an entry once its buffer is move-assigned another identity", "[PerBufferMap]") {
    PerBufferMap<std::string> map;
    Buffer                    target("target");
    map.Set(target, "target's entry");

    target = Buffer("replacement");
    REQUIRE(map.Find(target) == nullptr);
    REQUIRE(map.Size() == 0);
}

TEST_CASE("PerBufferMap::SetIfAbsent keeps a live entry and fills a missing one", "[PerBufferMap]") {
    PerBufferMap<std::string> map;
    Buffer                    buffer("buffer");
    REQUIRE(map.SetIfAbsent(buffer, "first") == "first");
    REQUIRE(map.SetIfAbsent(buffer, "ignored") == "first");
}

TEST_CASE("PerBufferMap takes its guard while forgetting a destroyed buffer", "[PerBufferMap]") {
    std::mutex                guard;
    PerBufferMap<std::string> map(&guard);
    auto                      buffer = std::make_unique<Buffer>("guarded");
    map.Set(*buffer, "entry");

    buffer.reset();
    REQUIRE(map.Size() == 0);
    // Not left locked.
    REQUIRE(guard.try_lock());
    guard.unlock();
}
