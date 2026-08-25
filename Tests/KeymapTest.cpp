#include <catch2/catch_test_macros.hpp>

#include <algorithm>

#include "Editor/Key.h"
#include "Editor/Keymap.h"

using ned::editor::FormatKeySequence;
using ned::editor::Keymap;
using ned::editor::KeymapStack;
using ned::editor::ParseKeySequence;

TEST_CASE("Resolve reports NoMatch for an unbound sequence", "[Keymap]") {
    Keymap keymap;
    const auto lookup = keymap.Resolve(ParseKeySequence("C-x"));
    REQUIRE(lookup.result == Keymap::LookupResult::NoMatch);
}

TEST_CASE("Bind then Resolve gives an exact Match", "[Keymap]") {
    Keymap keymap;
    keymap.Bind(ParseKeySequence("C-n"), "next-line");

    const auto lookup = keymap.Resolve(ParseKeySequence("C-n"));
    REQUIRE(lookup.result == Keymap::LookupResult::Match);
    REQUIRE(lookup.commandName == "next-line");
}

TEST_CASE("A partial prefix key sequence reports Prefix, not Match", "[Keymap]") {
    Keymap keymap;
    keymap.Bind(ParseKeySequence("C-x C-s"), "save-buffer");

    const auto partial = keymap.Resolve(ParseKeySequence("C-x"));
    REQUIRE(partial.result == Keymap::LookupResult::Prefix);

    const auto full = keymap.Resolve(ParseKeySequence("C-x C-s"));
    REQUIRE(full.result == Keymap::LookupResult::Match);
    REQUIRE(full.commandName == "save-buffer");
}

TEST_CASE("Unbind removes one binding without disturbing sibling bindings", "[Keymap]") {
    Keymap keymap;
    keymap.Bind(ParseKeySequence("C-x C-s"), "save-buffer");
    keymap.Bind(ParseKeySequence("C-x C-f"), "find-file");

    keymap.Unbind(ParseKeySequence("C-x C-s"));

    REQUIRE(keymap.Resolve(ParseKeySequence("C-x C-s")).result == Keymap::LookupResult::NoMatch);

    const auto stillBound = keymap.Resolve(ParseKeySequence("C-x C-f"));
    REQUIRE(stillBound.result == Keymap::LookupResult::Match);
    REQUIRE(stillBound.commandName == "find-file");

    // C-x is still a live prefix because C-x C-f survives.
    REQUIRE(keymap.Resolve(ParseKeySequence("C-x")).result == Keymap::LookupResult::Prefix);
}

TEST_CASE("KeymapStack prefers the higher-priority layer for an exact match", "[Keymap]") {
    Keymap minor;
    Keymap global;
    minor.Bind(ParseKeySequence("C-c"), "minor-mode-command");
    global.Bind(ParseKeySequence("C-c"), "global-command");

    const KeymapStack stack({&minor, &global});
    const auto         lookup = stack.Resolve(ParseKeySequence("C-c"));

    REQUIRE(lookup.result == Keymap::LookupResult::Match);
    REQUIRE(lookup.commandName == "minor-mode-command");
}

TEST_CASE("KeymapStack merges prefix continuations across layers", "[Keymap]") {
    // Layer A only continues C-x with C-s; layer B only continues C-x with C-f.
    Keymap layerA;
    Keymap layerB;
    layerA.Bind(ParseKeySequence("C-x C-s"), "save-buffer");
    layerB.Bind(ParseKeySequence("C-x C-f"), "find-file");

    const KeymapStack stack({&layerA, &layerB});

    // After just C-x, the stack should say Prefix (layer A has children there).
    REQUIRE(stack.Resolve(ParseKeySequence("C-x")).result == Keymap::LookupResult::Prefix);

    // C-x C-s resolves via layer A.
    const auto save = stack.Resolve(ParseKeySequence("C-x C-s"));
    REQUIRE(save.result == Keymap::LookupResult::Match);
    REQUIRE(save.commandName == "save-buffer");

    // C-x C-f isn't in layer A at all, but the stack still finds it in layer B.
    const auto find = stack.Resolve(ParseKeySequence("C-x C-f"));
    REQUIRE(find.result == Keymap::LookupResult::Match);
    REQUIRE(find.commandName == "find-file");
}

TEST_CASE("KeymapStack reports NoMatch only when no layer recognizes the sequence at all", "[Keymap]") {
    Keymap global;
    global.Bind(ParseKeySequence("C-n"), "next-line");

    const KeymapStack stack({&global});
    REQUIRE(stack.Resolve(ParseKeySequence("C-z")).result == Keymap::LookupResult::NoMatch);
}

TEST_CASE("AmbiguousBindings is empty for a keymap with no shorter-command/longer-sibling overlap", "[Keymap]") {
    Keymap keymap;
    keymap.Bind(ParseKeySequence("C-x C-s"), "save-buffer");
    keymap.Bind(ParseKeySequence("C-x C-f"), "find-file");
    keymap.Bind(ParseKeySequence("C-n"), "next-line");

    REQUIRE(keymap.AmbiguousBindings().empty());
}

TEST_CASE("AmbiguousBindings reports a sequence bound to a command that is also a prefix of a longer one", "[Keymap]") {
    Keymap keymap;
    keymap.Bind(ParseKeySequence("C-c a"), "org-agenda");
    keymap.Bind(ParseKeySequence("C-c a s"), "acp-start-session");

    const auto ambiguous = keymap.AmbiguousBindings();
    REQUIRE(ambiguous.size() == 1);
    REQUIRE(ambiguous.front() == FormatKeySequence(ParseKeySequence("C-c a")));
}

TEST_CASE("ChildrenAt lists the next chord of every sequence bound under a prefix", "[Keymap]") {
    Keymap keymap;
    keymap.Bind(ParseKeySequence("C-x C-s"), "save-buffer");
    keymap.Bind(ParseKeySequence("C-x C-f"), "find-file");
    keymap.Bind(ParseKeySequence("C-x 2"), "split-below");

    const auto children = keymap.ChildrenAt(ParseKeySequence("C-x"));
    REQUIRE(children.size() == 3);

    const auto find = [&](const ned::editor::KeyChord& chord) {
        return std::find_if(children.begin(), children.end(),
                             [&](const Keymap::ChildBinding& binding) { return binding.chord == chord; });
    };
    const auto saveChord = ParseKeySequence("C-s").front();
    const auto it        = find(saveChord);
    REQUIRE(it != children.end());
    REQUIRE(it->commandName == "save-buffer");
}

TEST_CASE("ChildrenAt reports a deeper, still-unbound chord with no commandName", "[Keymap]") {
    Keymap keymap;
    keymap.Bind(ParseKeySequence("C-x C-s"), "save-buffer");

    const auto children = keymap.ChildrenAt({});
    REQUIRE(children.size() == 1);
    REQUIRE_FALSE(children.front().commandName.has_value());
}

TEST_CASE("ChildrenAt is empty for a prefix that isn't bound as a prefix at all", "[Keymap]") {
    Keymap keymap;
    keymap.Bind(ParseKeySequence("C-n"), "next-line");

    REQUIRE(keymap.ChildrenAt(ParseKeySequence("C-x")).empty());
}

TEST_CASE("KeymapStack::ChildrenAt merges layers, first layer wins on overlap", "[Keymap]") {
    Keymap minor;
    Keymap global;
    minor.Bind(ParseKeySequence("C-c a"), "minor-command");
    global.Bind(ParseKeySequence("C-c a"), "org-agenda");
    global.Bind(ParseKeySequence("C-c b"), "global-only");

    const KeymapStack stack({&minor, &global});
    const auto        children = stack.ChildrenAt(ParseKeySequence("C-c"));
    REQUIRE(children.size() == 2);

    const auto find = [&](const ned::editor::KeyChord& chord) {
        return std::find_if(children.begin(), children.end(),
                             [&](const Keymap::ChildBinding& binding) { return binding.chord == chord; });
    };
    REQUIRE(find(ParseKeySequence("a").front())->commandName == "minor-command");
    REQUIRE(find(ParseKeySequence("b").front())->commandName == "global-only");
}
