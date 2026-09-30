#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <regex>
#include <string>
#include <utility>

#include "Text/Buffer.h"
#include "Text/PerBufferMap.h"

using ned::text::Buffer;
using ned::text::PerBufferMap;
using ned::text::PerBufferSet;

TEST_CASE("PerBufferMap finds what was stored for a buffer and nothing for another", "[PerBufferMap]") {
    PerBufferMap<std::string> map;
    Buffer                    a("a");
    Buffer                    b("b");
    map[&a] = "for a";

    REQUIRE(map.contains(&a));
    REQUIRE(map.find(&a)->second == "for a");
    REQUIRE(map.at(&a) == "for a");
    REQUIRE(map.find(&b) == map.end());

    REQUIRE(map.erase(&a) == 1);
    REQUIRE(map.erase(&a) == 0);
    REQUIRE(map.empty());
}

TEST_CASE("PerBufferMap drops a buffer's entry the moment the buffer is destroyed", "[PerBufferMap]") {
    PerBufferMap<std::string> map;
    Buffer                    kept("kept");
    map[&kept] = "kept's entry";
    {
        Buffer temporary("temporary");
        map.insert({&temporary, "temporary's entry"});
        REQUIRE(map.size() == 2);
    }
    REQUIRE(map.size() == 1);
    REQUIRE(map.find(&kept)->second == "kept's entry");
}

TEST_CASE("PerBufferMap never hands a freed buffer's entry to a new buffer at the same address", "[PerBufferMap]") {
    PerBufferMap<std::string> map;
    std::optional<Buffer>     slot;

    slot.emplace("first");
    const Buffer* address = &*slot;
    map[&*slot]           = "first's entry";
    slot.reset();

    slot.emplace("second");
    REQUIRE(&*slot == address);
    REQUIRE_FALSE(map.contains(&*slot));
}

TEST_CASE("PerBufferMap forgets a buffer in every container holding it", "[PerBufferMap]") {
    PerBufferMap<std::string> names;
    PerBufferMap<int>         counts;
    PerBufferSet<>            seen;
    {
        Buffer buffer("everywhere");
        names[&buffer]  = "name";
        counts[&buffer] = 1;
        seen.insert(&buffer);
    }
    REQUIRE(names.empty());
    REQUIRE(counts.empty());
    REQUIRE(seen.empty());
}

TEST_CASE("PerBufferMap destroyed before its buffers leaves them safe to destroy", "[PerBufferMap]") {
    Buffer                    buffer("outlives the map");
    PerBufferMap<std::string> survivor;
    survivor[&buffer] = "survivor";
    {
        PerBufferMap<std::string> shortLived;
        shortLived[&buffer] = "short-lived";
    }
    // The buffer's destruction must only reach `survivor`; ASan flags a
    // call into the destroyed map.
    REQUIRE(survivor.find(&buffer)->second == "survivor");
}

TEST_CASE("PerBufferMap erase and clear stop the buffer from reaching the map", "[PerBufferMap]") {
    PerBufferMap<std::string> map;
    {
        Buffer erasedByKey("erased by key");
        Buffer erasedByIterator("erased by iterator");
        Buffer cleared("cleared");
        map[&erasedByKey]      = "x";
        map[&erasedByIterator] = "y";
        map.erase(&erasedByKey);
        map.erase(map.find(&erasedByIterator));
        map[&cleared] = "z";
        map.clear();
        map[&erasedByKey] = "again";
        REQUIRE(map.size() == 1);
    }
    REQUIRE(map.empty());
}

TEST_CASE("PerBufferMap keys a moved buffer's entry to the object at the original address", "[PerBufferMap]") {
    PerBufferMap<std::string> map;
    auto                      source = std::make_unique<Buffer>("source");
    map[source.get()]                = "source's entry";

    Buffer moved(std::move(*source));
    REQUIRE_FALSE(map.contains(&moved));
    REQUIRE(map.size() == 1);

    source.reset();
    REQUIRE(map.empty());
}

TEST_CASE("PerBufferMap drops a buffer's entry when the buffer is assigned over", "[PerBufferMap]") {
    PerBufferMap<std::string> map;
    Buffer                    target("target");
    Buffer                    source("source");
    map[&target] = "target's entry";
    map[&source] = "source's entry";

    target = std::move(source);
    REQUIRE_FALSE(map.contains(&target));
    REQUIRE(map.contains(&source));
}

TEST_CASE("PerBufferMap try_emplace keeps a live entry and fills a missing one", "[PerBufferMap]") {
    PerBufferMap<std::string> map;
    Buffer                    buffer("buffer");
    REQUIRE(map.try_emplace(&buffer, "first").second);
    REQUIRE_FALSE(map.try_emplace(&buffer, "ignored").second);
    REQUIRE(map.find(&buffer)->second == "first");
}

TEST_CASE("PerBufferMap keyed by const Buffer* drops entries the same way", "[PerBufferMap]") {
    PerBufferMap<int, const Buffer*> map;
    {
        const Buffer buffer("const");
        map[&buffer] = 1;
        REQUIRE(map.size() == 1);
    }
    REQUIRE(map.empty());
}

TEST_CASE("PerBufferSet inserts a buffer once and forgets it when destroyed", "[PerBufferMap]") {
    PerBufferSet<> set;
    {
        Buffer buffer("member");
        REQUIRE(set.insert(&buffer).second);
        REQUIRE_FALSE(set.insert(&buffer).second);
        REQUIRE(set.contains(&buffer));
    }
    REQUIRE(set.empty());
}

TEST_CASE("PerBufferMap takes its guard while forgetting a destroyed buffer", "[PerBufferMap]") {
    std::mutex                guard;
    PerBufferMap<std::string> map(&guard);
    auto                      buffer = std::make_unique<Buffer>("guarded");
    map[buffer.get()]                = "entry";

    buffer.reset();
    REQUIRE(map.empty());
    // Not left locked.
    REQUIRE(guard.try_lock());
    guard.unlock();
}

// A container keyed by a raw Buffer* keeps its entries after the buffer is
// freed, and hands them to the next buffer allocated at that address.
TEST_CASE("No source file keys a container by a raw Buffer*", "[PerBufferMap]") {
    namespace fs          = std::filesystem;
    const fs::path   root = fs::path(NED_REPO_ROOT) / "Source";
    const std::regex rawKey(R"(\b(unordered_)?(multi)?(map|set)\s*<\s*(const\s+)?((ned::)?text::)?Buffer\s*(const\s*)?\*)");

    std::string offenders;
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        const fs::path& path = entry.path();
        if (!entry.is_regular_file() || (path.extension() != ".h" && path.extension() != ".cpp") ||
            path.filename() == "PerBufferMap.h") {
            continue;
        }
        std::ifstream in(path);
        std::string   line;
        for (std::size_t number = 1; std::getline(in, line); ++number) {
            if (const auto comment = line.find("//"); comment != std::string::npos) {
                line.erase(comment);
            }
            if (line.find("Buffer") != std::string::npos && std::regex_search(line, rawKey)) {
                offenders += fs::relative(path, root).generic_string() + ":" + std::to_string(number) + "\n";
            }
        }
    }
    INFO("Use text::PerBufferMap / text::PerBufferSet instead:\n"
         << offenders);
    REQUIRE(offenders.empty());
}
