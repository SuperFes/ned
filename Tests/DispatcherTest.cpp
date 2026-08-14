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

    KeyChord observed{};
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
