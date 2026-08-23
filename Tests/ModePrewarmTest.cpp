#include <catch2/catch_test_macros.hpp>

#include "Editor/HighlightSettings.h"
#include "Editor/ModeOverrides.h"
#include "Editor/ModePrewarm.h"
#include "Text/BufferList.h"
#include "UI/EventLoop.h"

using ned::editor::BuildWarmModeForPath;
using ned::editor::CachedModeForBuffer;
using ned::editor::ClearModeCacheFor;
using ned::editor::MaxHighlightBytes;
using ned::editor::ModePrewarmer;
using ned::editor::SetMaxHighlightBytes;

TEST_CASE("BuildWarmModeForPath resolves the same Mode name ModeForPath would", "[ModePrewarm]") {
    const ned::editor::Mode mode = BuildWarmModeForPath("/some/path/warm-test.cpp", "int main() { return 0; }");
    REQUIRE(mode.name == "cpp-mode");
}

TEST_CASE("BuildWarmModeForPath's returned Mode still highlights correctly afterward", "[ModePrewarm]") {
    // The whole point of prewarming is that the tree-sitter parse already
    // happened inside BuildWarmModeForPath itself -- this proves the
    // returned Mode is still fully functional (not, say, left in some
    // half-initialized state) by calling highlight() again and checking
    // real spans come back, the same assertion
    // "RegisterDynamicMode + ModeByName round-trip..." above already makes
    // for a freshly-resolved (non-prewarmed) Mode.
    const std::string_view  text = "int main() { return 0; }";
    const ned::editor::Mode mode = BuildWarmModeForPath("/some/path/warm-test2.cpp", text);
    REQUIRE(static_cast<bool>(mode.highlight));
    REQUIRE_FALSE(mode.highlight(text).empty());
}

TEST_CASE("BuildWarmModeForPath skips the highlight/fold warm-up past MaxHighlightBytes, same gate BufferView's own "
          "Paint uses",
          "[ModePrewarm]") {
    const std::size_t original = MaxHighlightBytes();
    SetMaxHighlightBytes(4); // smaller than the text below
    const ned::editor::Mode mode = BuildWarmModeForPath("/some/path/warm-test3.cpp", "int main() { return 0; }");
    SetMaxHighlightBytes(original); // restore process-wide state

    // Still resolves the real Mode -- only the eager warm-up pass is
    // skipped, not the resolution itself.
    REQUIRE(mode.name == "cpp-mode");
}

TEST_CASE("ModePrewarmer::ApplyPrewarmedMode installs the built Mode for a still-open buffer", "[ModePrewarm]") {
    ned::text::BufferList bufferList;
    ned::ui::EventLoop     eventLoop;
    ModePrewarmer          prewarmer(bufferList, eventLoop);

    ned::text::Buffer& buffer = bufferList.OpenOrCreateFile("/some/path/apply-test.apply-test-ext");

    ned::editor::Mode fake;
    fake.name = "fake-applied-mode";
    prewarmer.ApplyPrewarmedMode(buffer.Name(), fake);

    REQUIRE(CachedModeForBuffer(buffer).name == "fake-applied-mode");
    ClearModeCacheFor(buffer);
}

TEST_CASE("ModePrewarmer::ApplyPrewarmedMode is a safe no-op for a buffer name that's no longer open",
          "[ModePrewarm]") {
    ned::text::BufferList bufferList;
    ned::ui::EventLoop     eventLoop;
    ModePrewarmer          prewarmer(bufferList, eventLoop);

    ned::editor::Mode fake;
    fake.name = "fake-orphaned-mode";
    // No REQUIRE beyond "doesn't throw/crash" -- there's no buffer to
    // observe a result on, which is exactly the point: the background
    // prewarm thread's target buffer closed before the result came back.
    REQUIRE_NOTHROW(prewarmer.ApplyPrewarmedMode("never-opened-buffer-name", fake));
}

TEST_CASE("ModePrewarmer::Prewarm is a no-op for a buffer with no path", "[ModePrewarm]") {
    ned::text::BufferList bufferList;
    ned::ui::EventLoop     eventLoop;
    ModePrewarmer          prewarmer(bufferList, eventLoop);

    ned::text::Buffer& scratch = bufferList.CreateBuffer("prewarm-scratch-test");
    REQUIRE_FALSE(scratch.Path().has_value());
    REQUIRE_NOTHROW(prewarmer.Prewarm(scratch));
}
