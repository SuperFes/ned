#include <catch2/catch_test_macros.hpp>

#include "Editor/HighlightSettings.h"
#include "Editor/ModeOverrides.h"
#include "Editor/ModePrewarm.h"
#include "Text/BufferList.h"
#include "Text/RopeStorage.h"
#include "UI/EventLoop.h"

using ned::editor::BuildWarmModeForPath;
using ned::editor::BuildWarmModeForStorage;
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
    REQUIRE_FALSE(mode.highlight(text, ned::editor::HighlightWindow{}).empty());
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

namespace {

// Delegates everything to a real RopeStorage but records whether the
// whole-document materialize was asked for. The point of
// BuildWarmModeForStorage is that an oversized document never reaches
// ToString() at all, which is invisible in the Mode it returns -- so the
// call itself is what has to be observed.
class MaterializeCountingStorage : public ned::text::ITextStorage {
  public:
    explicit MaterializeCountingStorage(std::string_view text) : inner_(ned::text::Rope(text)) {
    }

    [[nodiscard]] int ToStringCalls() const {
        return toStringCalls_;
    }

    [[nodiscard]] std::string ToString() const override {
        ++toStringCalls_;
        return inner_.ToString();
    }

    [[nodiscard]] std::unique_ptr<ITextStorage> Clone() const override {
        return inner_.Clone();
    }
    [[nodiscard]] std::unique_ptr<ITextStorage> SnapshotForBackgroundRead() const override {
        return inner_.SnapshotForBackgroundRead();
    }
    [[nodiscard]] bool IsHuge() const override {
        return inner_.IsHuge();
    }
    [[nodiscard]] bool Empty() const override {
        return inner_.Empty();
    }
    [[nodiscard]] std::size_t ByteLength() const override {
        return inner_.ByteLength();
    }
    [[nodiscard]] std::size_t CodepointLength() const override {
        return inner_.CodepointLength();
    }
    [[nodiscard]] std::size_t LineCount() const override {
        return inner_.LineCount();
    }
    [[nodiscard]] std::unique_ptr<ITextStorage> Inserted(std::size_t byteOffset, std::string_view text) const override {
        return inner_.Inserted(byteOffset, text);
    }
    [[nodiscard]] std::unique_ptr<ITextStorage> Erased(std::size_t byteOffset, std::size_t byteLength) const override {
        return inner_.Erased(byteOffset, byteLength);
    }
    [[nodiscard]] std::string Substring(std::size_t byteOffset, std::size_t byteLength) const override {
        return inner_.Substring(byteOffset, byteLength);
    }
    [[nodiscard]] std::size_t ByteOffsetToLine(std::size_t byteOffset) const override {
        return inner_.ByteOffsetToLine(byteOffset);
    }
    [[nodiscard]] std::size_t LineToByteOffset(std::size_t line) const override {
        return inner_.LineToByteOffset(line);
    }
    [[nodiscard]] std::size_t ByteOffsetToCodepointOffset(std::size_t byteOffset) const override {
        return inner_.ByteOffsetToCodepointOffset(byteOffset);
    }
    [[nodiscard]] std::size_t CodepointOffsetToByteOffset(std::size_t codepointOffset) const override {
        return inner_.CodepointOffsetToByteOffset(codepointOffset);
    }
    [[nodiscard]] DecodedCodepoint CodepointAt(std::size_t byteOffset) const override {
        return inner_.CodepointAt(byteOffset);
    }
    [[nodiscard]] std::size_t PreviousCodepointBoundary(std::size_t byteOffset) const override {
        return inner_.PreviousCodepointBoundary(byteOffset);
    }
    [[nodiscard]] std::size_t NextCodepointBoundary(std::size_t byteOffset) const override {
        return inner_.NextCodepointBoundary(byteOffset);
    }
    void ForEachChunk(const std::function<void(std::string_view)>& sink) const override {
        inner_.ForEachChunk(sink);
    }

  private:
    ned::text::RopeStorage inner_;
    mutable int            toStringCalls_ = 0;
};

} // namespace

TEST_CASE("BuildWarmModeForStorage applies the size gate without materializing the document", "[ModePrewarm]") {
    // The gate has to be checked against the storage's own length, not
    // against an already-materialized string: Prewarm used to pass
    // snapshot->ToString() as the argument, so an oversized document was
    // copied in full and only then measured and discarded -- for a huge
    // buffer, a copy of the whole file. ByteLength() is O(1) on both
    // storage kinds, so the oversized case now costs nothing.
    const std::size_t original = MaxHighlightBytes();
    SetMaxHighlightBytes(4); // smaller than the storage below

    const MaterializeCountingStorage storage{"int main() { return 0; }"};
    const ned::editor::Mode          mode = BuildWarmModeForStorage("/some/path/warm-storage.cpp", storage);

    SetMaxHighlightBytes(original); // restore process-wide state

    REQUIRE(storage.ToStringCalls() == 0);
    // Same contract the string_view overload's own oversized case keeps:
    // the Mode still resolves, only the warm-up pass is skipped.
    REQUIRE(mode.name == "cpp-mode");
}

TEST_CASE("BuildWarmModeForStorage warms a document inside the size gate", "[ModePrewarm]") {
    const MaterializeCountingStorage storage{"int main() { return 0; }"};
    const ned::editor::Mode          mode = BuildWarmModeForStorage("/some/path/warm-storage2.cpp", storage);

    REQUIRE(storage.ToStringCalls() == 1);
    REQUIRE(mode.name == "cpp-mode");
    REQUIRE(static_cast<bool>(mode.highlight));
    REQUIRE_FALSE(mode.highlight("int main() { return 0; }", ned::editor::HighlightWindow{}).empty());
}

TEST_CASE("ModePrewarmer::ApplyPrewarmedMode installs the built Mode for a still-open buffer", "[ModePrewarm]") {
    ned::text::BufferList bufferList;
    ned::ui::EventLoop    eventLoop;
    ModePrewarmer         prewarmer(bufferList, eventLoop);

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
    ned::ui::EventLoop    eventLoop;
    ModePrewarmer         prewarmer(bufferList, eventLoop);

    ned::editor::Mode fake;
    fake.name = "fake-orphaned-mode";
    // No REQUIRE beyond "doesn't throw/crash" -- there's no buffer to
    // observe a result on, which is exactly the point: the background
    // prewarm thread's target buffer closed before the result came back.
    REQUIRE_NOTHROW(prewarmer.ApplyPrewarmedMode("never-opened-buffer-name", fake));
}

TEST_CASE("ModePrewarmer::Prewarm is a no-op for a buffer with no path", "[ModePrewarm]") {
    ned::text::BufferList bufferList;
    ned::ui::EventLoop    eventLoop;
    ModePrewarmer         prewarmer(bufferList, eventLoop);

    ned::text::Buffer& scratch = bufferList.CreateBuffer("prewarm-scratch-test");
    REQUIRE_FALSE(scratch.Path().has_value());
    REQUIRE_NOTHROW(prewarmer.Prewarm(scratch));
}
