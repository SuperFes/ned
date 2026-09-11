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
    mode.highlight = [calls](std::string_view text) {
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
