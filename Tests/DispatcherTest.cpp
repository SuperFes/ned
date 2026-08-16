#include <catch2/catch_test_macros.hpp>

#include "Editor/Dispatcher.h"
#include "Editor/Key.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "Text/KillRing.h"

using ned::editor::CommandContext;
using ned::editor::CommandRegistry;
using ned::editor::Dispatcher;
using ned::editor::Keymap;
using ned::editor::KeymapStack;
using ned::editor::ParseKeyChord;
using ned::editor::ParseKeySequence;

namespace {

struct Fixture {
    ned::text::Buffer     buffer{"scratch"};
    ned::text::KillRing   killRing;
    ned::text::BufferList bufferList;

    CommandContext Context() {
        return CommandContext{buffer, killRing, bufferList};
    }
};

} // namespace

TEST_CASE("Dispatcher invokes an immediately-bound single-key command", "[Dispatcher]") {
    CommandRegistry registry;
    registry.Register("insert-x", "", [](CommandContext& context) { context.buffer.InsertAtPoint("x"); });

    Keymap keymap;
    keymap.Bind(ParseKeySequence("C-x"), "insert-x");
    Dispatcher stack(registry, KeymapStack({&keymap}));

    Fixture        fixture;
    CommandContext context = fixture.Context();

    const auto outcome = stack.Feed(ParseKeyChord("C-x"), context);
    REQUIRE(outcome == Dispatcher::Outcome::Invoked);
    REQUIRE(fixture.buffer.Text() == "x");
}

TEST_CASE("Dispatcher reports Pending mid-sequence and Invoked on completion", "[Dispatcher]") {
    CommandRegistry registry;
    registry.Register("save-buffer", "", [](CommandContext& context) { context.buffer.InsertAtPoint("saved"); });

    Keymap keymap;
    keymap.Bind(ParseKeySequence("C-x C-s"), "save-buffer");
    Dispatcher dispatcher(registry, KeymapStack({&keymap}));

    Fixture        fixture;
    CommandContext context = fixture.Context();

    REQUIRE(dispatcher.Feed(ParseKeyChord("C-x"), context) == Dispatcher::Outcome::Pending);
    REQUIRE(dispatcher.Pending().size() == 1);

    REQUIRE(dispatcher.Feed(ParseKeyChord("C-s"), context) == Dispatcher::Outcome::Invoked);
    REQUIRE(fixture.buffer.Text() == "saved");
    REQUIRE(dispatcher.Pending().empty());
}

TEST_CASE("Dispatcher reports Unbound and clears pending on a dead sequence", "[Dispatcher]") {
    CommandRegistry registry;
    Keymap          keymap;
    keymap.Bind(ParseKeySequence("C-x C-s"), "save-buffer");
    Dispatcher dispatcher(registry, KeymapStack({&keymap}));

    Fixture        fixture;
    CommandContext context = fixture.Context();

    REQUIRE(dispatcher.Feed(ParseKeyChord("C-x"), context) == Dispatcher::Outcome::Pending);
    REQUIRE(dispatcher.Feed(ParseKeyChord("C-z"), context) == Dispatcher::Outcome::Unbound);
    REQUIRE(dispatcher.Pending().empty());
}

TEST_CASE("Reset discards an in-progress prefix sequence", "[Dispatcher]") {
    CommandRegistry registry;
    Keymap          keymap;
    keymap.Bind(ParseKeySequence("C-x C-s"), "save-buffer");
    Dispatcher dispatcher(registry, KeymapStack({&keymap}));

    Fixture        fixture;
    CommandContext context = fixture.Context();

    REQUIRE(dispatcher.Feed(ParseKeyChord("C-x"), context) == Dispatcher::Outcome::Pending);
    dispatcher.Reset();
    REQUIRE(dispatcher.Pending().empty());

    // C-s alone isn't bound, so this should be Unbound rather than resuming the old sequence.
    REQUIRE(dispatcher.Feed(ParseKeyChord("C-s"), context) == Dispatcher::Outcome::Unbound);
}

TEST_CASE("triggeringKey reflects the chord that completed the match", "[Dispatcher]") {
    using ned::editor::KeyChord;

    KeyChord        observed{};
    CommandRegistry registry;
    registry.Register("observe", "", [&observed](CommandContext& context) { observed = context.triggeringKey; });

    Keymap keymap;
    keymap.Bind(ParseKeySequence("C-x C-s"), "observe");
    Dispatcher dispatcher(registry, KeymapStack({&keymap}));

    Fixture        fixture;
    CommandContext context = fixture.Context();

    dispatcher.Feed(ParseKeyChord("C-x"), context);
    dispatcher.Feed(ParseKeyChord("C-s"), context);

    REQUIRE(observed == ParseKeyChord("C-s"));
}

// Keyboard macros (kmacro-start-macro/kmacro-end-or-call-macro follow-up).

TEST_CASE("Recording captures a full multi-chord sequence as one unit", "[Dispatcher]") {
    using ned::editor::KeyChord;

    CommandRegistry registry;
    registry.Register("save-buffer", "", [](CommandContext& context) { context.buffer.InsertAtPoint("saved"); });

    Keymap keymap;
    keymap.Bind(ParseKeySequence("C-x C-s"), "save-buffer");
    Dispatcher dispatcher(registry, KeymapStack({&keymap}));

    Fixture        fixture;
    CommandContext context = fixture.Context();

    dispatcher.StartRecording();
    dispatcher.Feed(ParseKeyChord("C-x"), context);
    dispatcher.Feed(ParseKeyChord("C-s"), context);
    dispatcher.StopRecording();

    const std::vector<KeyChord> expected{ParseKeyChord("C-x"), ParseKeyChord("C-s")};
    REQUIRE(dispatcher.LastMacro() == expected);
}

TEST_CASE("An unbound chord during recording isn't captured", "[Dispatcher]") {
    CommandRegistry registry;
    Keymap          keymap; // nothing bound at all
    Dispatcher      dispatcher(registry, KeymapStack({&keymap}));

    Fixture        fixture;
    CommandContext context = fixture.Context();

    dispatcher.StartRecording();
    REQUIRE(dispatcher.Feed(ParseKeyChord("z"), context) == Dispatcher::Outcome::Unbound);
    dispatcher.StopRecording();

    REQUIRE(dispatcher.LastMacro().empty());
}

TEST_CASE("The stop command's own triggering chord is excluded from the recorded macro", "[Dispatcher]") {
    using ned::editor::KeyChord;

    CommandRegistry registry;
    registry.Register("insert-a", "", [](CommandContext& context) { context.buffer.InsertAtPoint("a"); });

    Keymap keymap;
    keymap.Bind(ParseKeySequence("x"), "insert-a");
    Dispatcher dispatcher(registry, KeymapStack({&keymap}));

    // Registered/bound after Dispatcher's own construction -- both registry_
    // and the KeymapStack's Keymap* are held by reference/pointer, so this
    // is visible to Feed() the same way BufferView's real command lambdas
    // (which capture a real Dispatcher&, not available at this test's
    // construction time either) end up wired.
    registry.Register("stop-macro", "", [&dispatcher](CommandContext&) {
        // Mirrors what BufferView actually does for kmacro-end-or-call-macro
        // -- see Dispatcher::DiscardMostRecentlyRecordedChords's own doc
        // comment for why both calls are the caller's responsibility, not
        // something Feed/StopRecording do on their own.
        dispatcher.DiscardMostRecentlyRecordedChords();
        dispatcher.StopRecording();
    });
    keymap.Bind(ParseKeySequence("F4"), "stop-macro");

    Fixture        fixture;
    CommandContext context = fixture.Context();

    dispatcher.StartRecording();
    dispatcher.Feed(ParseKeyChord("x"), context);
    dispatcher.Feed(ParseKeyChord("F4"), context);

    const std::vector<KeyChord> expected{ParseKeyChord("x")};
    REQUIRE(dispatcher.LastMacro() == expected);
}

TEST_CASE("StopRecording while not recording is a safe no-op", "[Dispatcher]") {
    CommandRegistry registry;
    Keymap          keymap;
    Dispatcher      dispatcher(registry, KeymapStack({&keymap}));

    REQUIRE_FALSE(dispatcher.IsRecording());
    dispatcher.StopRecording();
    REQUIRE_FALSE(dispatcher.IsRecording());
    REQUIRE(dispatcher.LastMacro().empty());
}

TEST_CASE("Starting a new recording doesn't clear the previous LastMacro until the new one finishes",
          "[Dispatcher]") {
    CommandRegistry registry;
    registry.Register("noop", "", [](CommandContext&) {});
    Keymap keymap;
    keymap.Bind(ParseKeySequence("x"), "noop");
    Dispatcher dispatcher(registry, KeymapStack({&keymap}));

    Fixture        fixture;
    CommandContext context = fixture.Context();

    dispatcher.StartRecording();
    dispatcher.Feed(ParseKeyChord("x"), context);
    dispatcher.StopRecording();
    REQUIRE(dispatcher.LastMacro().size() == 1);

    dispatcher.StartRecording();                 // begin a second recording
    REQUIRE(dispatcher.LastMacro().size() == 1); // previous macro still intact mid-recording
}
