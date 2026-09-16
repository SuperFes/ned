#include <catch2/catch_test_macros.hpp>

#include <string>

#include "Editor/HighlightCache.h"
#include "Editor/Mode.h"
#include "Text/Buffer.h"

using ned::editor::CachedHighlightSpans;
using ned::editor::ClearHighlightCache;

// One per-buffer cache shared by everything that paints highlighting.
// BufferView and Minimap each kept their own, so with the minimap on (the
// default) every keystroke ran the whole-document highlight twice.

namespace {

struct CacheGuard {
    CacheGuard() {
        ClearHighlightCache();
    }
    ~CacheGuard() {
        ClearHighlightCache();
    }
};

// A Mode whose highlight function counts how often it actually runs -- the
// only thing this cache exists to reduce.
ned::editor::Mode CountingMode(std::shared_ptr<int> calls, std::string name = "counting-mode") {
    ned::editor::Mode mode;
    mode.name      = std::move(name);
    mode.highlight = [calls](std::string_view text, ned::editor::HighlightWindow) {
        ++*calls;
        return std::vector<ned::editor::HighlightSpan>{
            {.startByte = 0, .endByte = text.size(), .syntaxClass = ned::editor::SyntaxClass::Comment}};
    };
    return mode;
}

} // namespace

TEST_CASE("Two consumers in one frame share a single highlight run", "[HighlightCache]") {
    const CacheGuard  guard;
    const auto        calls = std::make_shared<int>(0);
    ned::text::Buffer buffer{"scratch"};
    buffer.InsertAtPoint("hello world");
    const ned::editor::Mode mode = CountingMode(calls);

    const auto first  = CachedHighlightSpans(buffer, mode);
    const auto second = CachedHighlightSpans(buffer, mode);

    REQUIRE(*calls == 1);     // the whole point
    REQUIRE(first == second); // and the same object, so neither copies it
    REQUIRE(first->size() == 1);
}

TEST_CASE("An edit invalidates the entry, and nothing else re-runs it", "[HighlightCache]") {
    const CacheGuard  guard;
    const auto        calls = std::make_shared<int>(0);
    ned::text::Buffer buffer{"scratch"};
    buffer.InsertAtPoint("hello");
    const ned::editor::Mode mode = CountingMode(calls);

    CachedHighlightSpans(buffer, mode);
    REQUIRE(*calls == 1);

    buffer.InsertAtPoint("!"); // content generation moves
    CachedHighlightSpans(buffer, mode);
    REQUIRE(*calls == 2);

    // Repeated frames with no edit must not re-run it, however many ask.
    for (int i = 0; i < 10; ++i) {
        CachedHighlightSpans(buffer, mode);
    }
    REQUIRE(*calls == 2);
}

TEST_CASE("A different mode for the same buffer is a different entry", "[HighlightCache]") {
    const CacheGuard  guard;
    const auto        calls = std::make_shared<int>(0);
    ned::text::Buffer buffer{"scratch"};
    buffer.InsertAtPoint("hello");

    CachedHighlightSpans(buffer, CountingMode(calls, "mode-a"));
    CachedHighlightSpans(buffer, CountingMode(calls, "mode-b"));
    REQUIRE(*calls == 2); // a mode switch must not serve the old mode's spans

    CachedHighlightSpans(buffer, CountingMode(calls, "mode-b"));
    REQUIRE(*calls == 2); // ...and then caches normally
}

TEST_CASE("A mode with no highlight function runs nothing and yields nothing", "[HighlightCache]") {
    const CacheGuard  guard;
    ned::text::Buffer buffer{"scratch"};
    buffer.InsertAtPoint("hello");

    const ned::editor::Mode plain;
    const auto              spans = CachedHighlightSpans(buffer, plain);
    REQUIRE(spans != nullptr); // never null, so no caller needs to check
    REQUIRE(spans->empty());
}

TEST_CASE("The cache is bounded, and one buffer cannot fill it with its own history", "[HighlightCache]") {
    const CacheGuard        guard;
    const auto              calls = std::make_shared<int>(0);
    ned::text::Buffer       buffer{"scratch"};
    const ned::editor::Mode mode = CountingMode(calls);

    // Twenty edits to one buffer, well past the entry cap: each is a miss,
    // but they must not evict anything else by piling up stale entries.
    for (int i = 0; i < 20; ++i) {
        buffer.InsertAtPoint("x");
        CachedHighlightSpans(buffer, mode);
    }
    REQUIRE(*calls == 20);

    ned::text::Buffer other{"other"};
    other.InsertAtPoint("hello");
    CachedHighlightSpans(other, mode);
    const int afterOther = *calls;
    CachedHighlightSpans(buffer, mode); // still cached despite 20 edits' worth of churn
    REQUIRE(*calls == afterOther);
}

// Not tested here: the minimap's own debounce. Its highlight runs inside the
// raster path behind EnsurePlane(), which needs a real ncplane, so a headless
// Screen never reaches it -- painting a Minimap in a test exercises none of
// it. The evidence for that one is Tests/KeystrokeBench.cpp, where a frame
// painting a BufferView *and* a Minimap over a 127 KiB markdown buffer costs
// 80ms per keystroke against 134ms before, i.e. the minimap now adds ~5ms
// rather than doubling the whole-document highlight.

// dangling-buffer-pointer-collision follow-up: a real bug found while
// digging into the ROADMAP watch-list's "highlight cache updates flaky
// under --order rand" entry, not a hypothetical. This cache's Entry keys on
// `const text::Buffer*` alone plus a handful of generation/name checks --
// none of which are true LIFETIME facts, so a Buffer with no hook into this
// cache's invalidation (a stack-local test Fixture, e.g. Tests/
// BufferViewTest.cpp's) can be destroyed, and an entirely unrelated LATER
// Buffer can be constructed at the exact same address with the exact same
// contentGeneration/modeName/window this cache would otherwise accept as a
// match -- a dangling-pointer cache HIT serving one buffer's spans for
// another buffer's content. Reproduced here with placement new rather than
// relying on incidental stack-layout luck, so this is deterministic: A is
// destroyed and B is placement-constructed at the exact same storage A
// occupied, matching every one of the old cache-key fields A's entry set.
TEST_CASE("A buffer destroyed and replaced at the same address never inherits the dead buffer's "
          "cached spans",
          "[HighlightCache]") {
    const CacheGuard        guard;
    const auto              calls = std::make_shared<int>(0);
    const ned::editor::Mode mode  = CountingMode(calls);

    alignas(ned::text::Buffer) unsigned char storage[sizeof(ned::text::Buffer)];

    ned::text::Buffer* a = new (storage) ned::text::Buffer("scratch");
    a->InsertAtPoint("hello"); // contentGeneration -> 1
    const auto spansA = CachedHighlightSpans(*a, mode);
    REQUIRE(*calls == 1);
    REQUIRE(spansA->size() == 1);
    REQUIRE((*spansA)[0].endByte == 5); // "hello".size()
    a->~Buffer();

    // B lands at the exact same address A did, same contentGeneration
    // (also 1, after its own single InsertAtPoint), same mode name, same
    // default (whole-buffer) window -- everything the old, InstanceId-less
    // key checked, all matching by coincidence.
    ned::text::Buffer* b = new (storage) ned::text::Buffer("scratch");
    b->InsertAtPoint("hi");
    const auto spansB = CachedHighlightSpans(*b, mode);

    REQUIRE(*calls == 2); // a real miss -- B's own content was actually highlighted
    REQUIRE(spansB->size() == 1);
    REQUIRE((*spansB)[0].endByte == 2); // "hi".size(), not "hello"'s 5
    b->~Buffer();
}
