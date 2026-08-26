#include <catch2/catch_test_macros.hpp>

#include "Editor/Dispatcher.h"
#include "Editor/Key.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "Text/KillRing.h"
#include "Text/Utf8.h"

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

// Prefix arguments (C-u / universal-argument follow-up).

TEST_CASE("A prefix arg invokes the matched command that many times", "[Dispatcher]") {
    CommandRegistry registry;
    registry.Register("insert-x", "", [](CommandContext& context) { context.buffer.InsertAtPoint("x"); });

    Keymap keymap;
    keymap.Bind(ParseKeySequence("C-x"), "insert-x");
    Dispatcher dispatcher(registry, KeymapStack({&keymap}));

    Fixture        fixture;
    CommandContext context = fixture.Context();
    context.prefixArg      = 5;

    REQUIRE(dispatcher.Feed(ParseKeyChord("C-x"), context) == Dispatcher::Outcome::Invoked);
    REQUIRE(fixture.buffer.Text() == "xxxxx");
}

TEST_CASE("A repeated invoke is grouped into one undo step", "[Dispatcher]") {
    CommandRegistry registry;
    registry.Register("insert-x", "", [](CommandContext& context) { context.buffer.InsertAtPoint("x"); });

    Keymap keymap;
    keymap.Bind(ParseKeySequence("C-x"), "insert-x");
    Dispatcher dispatcher(registry, KeymapStack({&keymap}));

    Fixture        fixture;
    CommandContext context = fixture.Context();
    context.prefixArg      = 5;

    dispatcher.Feed(ParseKeyChord("C-x"), context);
    REQUIRE(fixture.buffer.Text() == "xxxxx");

    fixture.buffer.Undo();
    REQUIRE(fixture.buffer.Text().empty());
}

TEST_CASE("A negative prefix arg on a paired motion command flips direction", "[Dispatcher]") {
    CommandRegistry registry;
    registry.Register("forward-char", "", [](CommandContext& context) { context.buffer.MoveForward(); });
    registry.Register("backward-char", "", [](CommandContext& context) { context.buffer.MoveBackward(); });

    Keymap keymap;
    keymap.Bind(ParseKeySequence("C-f"), "forward-char");
    Dispatcher dispatcher(registry, KeymapStack({&keymap}));

    Fixture fixture;
    fixture.buffer.InsertAtPoint("hello");
    fixture.buffer.SetPoint(3);
    CommandContext context = fixture.Context();
    context.prefixArg      = -2;

    dispatcher.Feed(ParseKeyChord("C-f"), context);
    REQUIRE(fixture.buffer.Point() == 1);
}

TEST_CASE("A prefix arg of zero runs the command zero times", "[Dispatcher]") {
    CommandRegistry registry;
    registry.Register("insert-x", "", [](CommandContext& context) { context.buffer.InsertAtPoint("x"); });

    Keymap keymap;
    keymap.Bind(ParseKeySequence("C-x"), "insert-x");
    Dispatcher dispatcher(registry, KeymapStack({&keymap}));

    Fixture        fixture;
    CommandContext context = fixture.Context();
    context.prefixArg      = 0;

    REQUIRE(dispatcher.Feed(ParseKeyChord("C-x"), context) == Dispatcher::Outcome::Invoked);
    REQUIRE(fixture.buffer.Text().empty());
}

TEST_CASE("A prefix arg persists across a Pending outcome and clears once Invoked", "[Dispatcher]") {
    CommandRegistry registry;
    registry.Register("save-buffer", "", [](CommandContext& context) { context.buffer.InsertAtPoint("x"); });

    Keymap keymap;
    keymap.Bind(ParseKeySequence("C-x C-s"), "save-buffer");
    Dispatcher dispatcher(registry, KeymapStack({&keymap}));

    Fixture        fixture;
    CommandContext context = fixture.Context();
    context.prefixArg      = 3;

    REQUIRE(dispatcher.Feed(ParseKeyChord("C-x"), context) == Dispatcher::Outcome::Pending);
    REQUIRE(context.prefixArg == 3); // untouched mid-sequence

    REQUIRE(dispatcher.Feed(ParseKeyChord("C-s"), context) == Dispatcher::Outcome::Invoked);
    REQUIRE(fixture.buffer.Text() == "xxx");
    REQUIRE_FALSE(context.prefixArg.has_value());
}

TEST_CASE("An unbound key cancels a pending prefix arg", "[Dispatcher]") {
    CommandRegistry registry;
    Keymap          keymap;
    keymap.Bind(ParseKeySequence("C-x C-s"), "save-buffer");
    Dispatcher dispatcher(registry, KeymapStack({&keymap}));

    Fixture        fixture;
    CommandContext context = fixture.Context();
    context.prefixArg      = 3;

    REQUIRE(dispatcher.Feed(ParseKeyChord("C-x"), context) == Dispatcher::Outcome::Pending);
    REQUIRE(dispatcher.Feed(ParseKeyChord("C-z"), context) == Dispatcher::Outcome::Unbound);
    REQUIRE_FALSE(context.prefixArg.has_value());
}

// self-insert-fallback follow-up: the default global keymap only gives
// printable ASCII (0x20-0x7E) its own real self-insert-command entry (see
// BuildDefaultGlobalKeymap's own comment in Commands.cpp) -- everything
// else (accented Latin, CJK, emoji, ...) used to report Unbound and never
// reach the buffer at all, confirmed live via a real paste/keystroke of an
// emoji. These tests exercise Dispatcher::Feed's own fallback directly
// against a minimal stand-in self-insert-command (mirroring this file's
// own "insert-x"/"save-buffer" stand-ins elsewhere, not the real
// Commands.cpp registration with its auto-pair logic).
namespace {

CommandRegistry RegistryWithSelfInsertStandIn() {
    CommandRegistry registry;
    registry.Register("self-insert-command", "", [](CommandContext& context) {
        context.buffer.InsertAtPoint(ned::text::EncodeCodepointUtf8(context.triggeringKey.Codepoint));
    });
    return registry;
}

} // namespace

TEST_CASE("An otherwise-unbound plain non-ASCII codepoint falls through to self-insert-command", "[Dispatcher]") {
    using ned::editor::KeyChord;

    CommandRegistry registry = RegistryWithSelfInsertStandIn();
    Keymap          keymap; // nothing bound at all -- every chord below is a real NoMatch
    Dispatcher      dispatcher(registry, KeymapStack({&keymap}));

    Fixture        fixture;
    CommandContext context = fixture.Context();

    KeyChord accented{};
    accented.Codepoint = U'é'; // 'é', a 2-byte UTF-8 codepoint

    REQUIRE(dispatcher.Feed(accented, context) == Dispatcher::Outcome::Invoked);
    REQUIRE(fixture.buffer.Text() == ned::text::EncodeCodepointUtf8(U'é'));

    KeyChord emoji{};
    emoji.Codepoint = U'\U0001F635'; // outside the BMP -- a 4-byte UTF-8 codepoint
    REQUIRE(dispatcher.Feed(emoji, context) == Dispatcher::Outcome::Invoked);
    REQUIRE(fixture.buffer.Text() == ned::text::EncodeCodepointUtf8(U'é') + ned::text::EncodeCodepointUtf8(U'\U0001F635'));
}

TEST_CASE("The self-insert fallback is a no-op (stays Unbound) when self-insert-command isn't registered",
          "[Dispatcher]") {
    using ned::editor::KeyChord;

    CommandRegistry registry; // deliberately no self-insert-command entry
    Keymap          keymap;
    Dispatcher      dispatcher(registry, KeymapStack({&keymap}));

    Fixture        fixture;
    CommandContext context = fixture.Context();

    KeyChord accented{};
    accented.Codepoint = U'é';
    REQUIRE(dispatcher.Feed(accented, context) == Dispatcher::Outcome::Unbound);
    REQUIRE(fixture.buffer.Text().empty());
}

TEST_CASE("A Control- or Meta-modified non-ASCII chord does not fall through to self-insert", "[Dispatcher]") {
    using ned::editor::KeyChord;

    CommandRegistry registry = RegistryWithSelfInsertStandIn();
    Keymap          keymap;
    Dispatcher      dispatcher(registry, KeymapStack({&keymap}));

    Fixture        fixture;
    CommandContext context = fixture.Context();

    KeyChord controlAccented{};
    controlAccented.Codepoint = U'é';
    controlAccented.Control   = true;
    REQUIRE(dispatcher.Feed(controlAccented, context) == Dispatcher::Outcome::Unbound);

    KeyChord metaAccented{};
    metaAccented.Codepoint = U'é';
    metaAccented.Meta      = true;
    REQUIRE(dispatcher.Feed(metaAccented, context) == Dispatcher::Outcome::Unbound);

    REQUIRE(fixture.buffer.Text().empty());
}

TEST_CASE("A control-range codepoint (C0/DEL/C1) does not fall through to self-insert", "[Dispatcher]") {
    using ned::editor::KeyChord;

    CommandRegistry registry = RegistryWithSelfInsertStandIn();
    Keymap          keymap;
    Dispatcher      dispatcher(registry, KeymapStack({&keymap}));

    Fixture        fixture;
    CommandContext context = fixture.Context();

    for (const char32_t codepoint : {char32_t{0x01}, char32_t{0x7F}, char32_t{0x85}}) {
        KeyChord chord{};
        chord.Codepoint = codepoint;
        REQUIRE(dispatcher.Feed(chord, context) == Dispatcher::Outcome::Unbound);
    }
    REQUIRE(fixture.buffer.Text().empty());
}

TEST_CASE("An unbound non-ASCII chord after a prefix sequence stays Unbound, not self-inserted", "[Dispatcher]") {
    using ned::editor::KeyChord;

    CommandRegistry registry = RegistryWithSelfInsertStandIn();
    Keymap          keymap;
    keymap.Bind(ParseKeySequence("C-c C-c"), "self-insert-command"); // gives "C-c" a real Prefix node
    Dispatcher dispatcher(registry, KeymapStack({&keymap}));

    Fixture        fixture;
    CommandContext context = fixture.Context();

    REQUIRE(dispatcher.Feed(ParseKeyChord("C-c"), context) == Dispatcher::Outcome::Pending);

    KeyChord accented{};
    accented.Codepoint = U'é';
    // "C-c é" isn't bound to anything -- must report Unbound, not silently
    // insert 'é' as if the prefix never happened.
    REQUIRE(dispatcher.Feed(accented, context) == Dispatcher::Outcome::Unbound);
    REQUIRE(fixture.buffer.Text().empty());
}
