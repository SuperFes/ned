#include <catch2/catch_test_macros.hpp>

#include <string>

#include "Editor/Commands.h"
#include "Editor/Dispatcher.h"
#include "Editor/Keymap.h"
#include "Editor/Mode.h"
#include "Editor/Multibuffer.h"
#include "Editor/NextError.h"
#include "Editor/PromptHistory.h"
#include "Editor/Register.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "Text/KillRing.h"
#include "UI/ActiveBuffer.h"
#include "UI/BufferView.h"
#include "UI/Theme.h"

using ned::editor::ClearLastResultsBufferForTesting;
using ned::editor::SetLastResultsBuffer;
using ned::editor::multibuffer::BuildMultibuffer;
using ned::editor::multibuffer::ClearRegistryForTesting;
using ned::editor::multibuffer::ExcerptSource;
using ned::ui::BufferView;

namespace {

// Both statics NextError()/PreviousError() read from (Multibuffer.h's
// registry, NextError.h's "last results buffer" name) are process-wide --
// same ResetGuard shape NextErrorTest.cpp/MultibufferTest.cpp already use.
struct ResetGuard {
    ResetGuard() {
        ClearRegistryForTesting();
        ClearLastResultsBufferForTesting();
    }
    ~ResetGuard() {
        ClearRegistryForTesting();
        ClearLastResultsBufferForTesting();
    }
};

// Mirrors BufferViewHunkNavigationTest.cpp's own Fixture shape.
struct Fixture {
    ResetGuard                 resetGuard;
    ned::text::Buffer          buffer{"scratch"};
    ned::text::KillRing        killRing;
    ned::editor::RegisterTable registers;
    ned::editor::PromptHistory promptHistory;
    ned::text::BufferList      bufferList;

    ned::editor::CommandRegistry registry{[] {
        ned::editor::CommandRegistry r;
        ned::editor::RegisterBuiltinCommands(r);
        return r;
    }()};
    ned::editor::Keymap          keymap = ned::editor::BuildDefaultGlobalKeymap();
    ned::editor::Dispatcher      dispatcher{registry, ned::editor::KeymapStack({&keymap})};
    ned::editor::Mode            mode  = ned::editor::FundamentalMode();
    ned::ui::Theme               theme = ned::ui::DarkTheme();

    std::string           statusMessage;
    ned::ui::ActiveBuffer activeBuffer{buffer};

    BufferView View() {
        return BufferView(activeBuffer, killRing, registers, promptHistory, bufferList, dispatcher, statusMessage,
                          mode, theme);
    }
};

} // namespace

TEST_CASE("NextError/PreviousError report no results when nothing was ever built", "[BufferView][NextError]") {
    Fixture    fixture;
    BufferView view = fixture.View();

    view.NextErrorForTesting();
    REQUIRE(fixture.statusMessage == "No results to step through.");

    view.PreviousErrorForTesting();
    REQUIRE(fixture.statusMessage == "No results to step through.");
}

TEST_CASE("NextError walks forward through a flat \"path:line:\" results buffer, jumping into each source file",
          "[BufferView][NextError]") {
    Fixture fixture;

    // JumpToPathLine's line/column math needs real content at the target
    // line -- OpenOrCreateFile's dedupe-by-path (Text/BufferList.h) means
    // pre-populating these here is what NextErrorForTesting's own jump
    // lands on, not a fresh empty buffer.
    fixture.bufferList.OpenOrCreateFile("alpha.txt").InsertAtPoint("l1\nl2\nl3\nl4\n");
    fixture.bufferList.OpenOrCreateFile("beta.txt").InsertAtPoint("l1\nl2\nl3\nl4\nl5\nl6\nl7\nl8\nl9\nl10\n");

    ned::text::Buffer& results = fixture.bufferList.CreateBuffer("*search results*");
    results.InsertAtPoint("alpha.txt:3: first\n"
                          "beta.txt:9: second\n");
    results.SetPoint(0);
    results.SetReadOnly(true);
    SetLastResultsBuffer("*search results*");

    BufferView view = fixture.View();

    view.NextErrorForTesting();
    REQUIRE(fixture.activeBuffer.Get().Path()->string() == "alpha.txt");
    REQUIRE(fixture.activeBuffer.Get().Content().ByteOffsetToLine(fixture.activeBuffer.Get().Point()) == 2); // line 3, 0-indexed
    REQUIRE(fixture.statusMessage.empty());

    view.NextErrorForTesting();
    REQUIRE(fixture.activeBuffer.Get().Path()->string() == "beta.txt");
    REQUIRE(fixture.activeBuffer.Get().Content().ByteOffsetToLine(fixture.activeBuffer.Get().Point()) == 8); // line 9, 0-indexed

    // No more results below point -- a no-op, status message explains why.
    view.NextErrorForTesting();
    REQUIRE(fixture.activeBuffer.Get().Path()->string() == "beta.txt");
    REQUIRE(fixture.statusMessage == "No more errors below point.");
}

TEST_CASE("PreviousError walks backward through a flat results buffer", "[BufferView][NextError]") {
    Fixture fixture;

    ned::text::Buffer& results = fixture.bufferList.CreateBuffer("*search results*");
    results.InsertAtPoint("alpha.txt:3: first\n"
                          "beta.txt:9: second\n");
    results.SetPoint(0);
    results.SetReadOnly(true);
    SetLastResultsBuffer("*search results*");

    BufferView view = fixture.View();

    // Walk to the end first, same as the forward test, then unwind.
    view.NextErrorForTesting();
    view.NextErrorForTesting();
    REQUIRE(fixture.activeBuffer.Get().Path()->string() == "beta.txt");

    view.PreviousErrorForTesting();
    REQUIRE(fixture.activeBuffer.Get().Path()->string() == "alpha.txt");
    REQUIRE(fixture.statusMessage.empty());

    // No more results above point.
    view.PreviousErrorForTesting();
    REQUIRE(fixture.activeBuffer.Get().Path()->string() == "alpha.txt");
    REQUIRE(fixture.statusMessage == "No more errors above point.");
}

TEST_CASE("NextError walks a MultibufferIndex-backed results buffer by excerpt", "[BufferView][NextError]") {
    Fixture fixture;

    // Same pre-population reasoning as the flat-buffer test above.
    fixture.bufferList.OpenOrCreateFile("one.cpp").InsertAtPoint("l1\nl2\nl3\nl4\nl5\nl6\n");
    fixture.bufferList.OpenOrCreateFile("two.cpp").InsertAtPoint("l1\nl2\nl3\nl4\nl5\nl6\nl7\nl8\n");

    std::vector<ExcerptSource> excerpts;
    excerpts.push_back(ExcerptSource{"one.cpp", 5, 5, "one.cpp:5", "body one\n"});
    excerpts.push_back(ExcerptSource{"two.cpp", 7, 7, "two.cpp:7", "body two\n"});
    BuildMultibuffer(fixture.bufferList, "*diagnostics*", excerpts);
    SetLastResultsBuffer("*diagnostics*");

    BufferView view = fixture.View();

    view.NextErrorForTesting();
    REQUIRE(fixture.activeBuffer.Get().Path()->string() == "one.cpp");
    REQUIRE(fixture.activeBuffer.Get().Content().ByteOffsetToLine(fixture.activeBuffer.Get().Point()) == 4); // line 5, 0-indexed

    view.NextErrorForTesting();
    REQUIRE(fixture.activeBuffer.Get().Path()->string() == "two.cpp");
    REQUIRE(fixture.activeBuffer.Get().Content().ByteOffsetToLine(fixture.activeBuffer.Get().Point()) == 6); // line 7, 0-indexed
}

TEST_CASE("NextError keeps working after focus moved to an unrelated source buffer", "[BufferView][NextError]") {
    Fixture fixture;

    ned::text::Buffer& results = fixture.bufferList.CreateBuffer("*search results*");
    results.InsertAtPoint("alpha.txt:3: first\n"
                          "beta.txt:9: second\n");
    results.SetPoint(0);
    results.SetReadOnly(true);
    SetLastResultsBuffer("*search results*");

    BufferView view = fixture.View();
    view.NextErrorForTesting(); // now showing alpha.txt

    // The user edits some other, unrelated buffer -- next-error must still
    // resume from the results buffer's own remembered position, not from
    // wherever activeBuffer_ happens to be pointing now (real Emacs' own
    // next-error-last-buffer semantics -- see NextError.h's own doc comment).
    ned::text::Buffer& unrelated = fixture.bufferList.CreateBuffer("unrelated");
    fixture.activeBuffer.Set(unrelated);

    view.NextErrorForTesting();
    REQUIRE(fixture.activeBuffer.Get().Path()->string() == "beta.txt");
}
