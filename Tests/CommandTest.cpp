#include <catch2/catch_test_macros.hpp>

#include <stdexcept>

#include "Editor/Command.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "Text/KillRing.h"

using ned::editor::Command;
using ned::editor::CommandContext;
using ned::editor::CommandRegistry;
using ned::editor::CompleteCommandNames;

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

TEST_CASE("Register makes a command findable", "[Command]") {
    CommandRegistry registry;
    registry.Register("noop", "Does nothing.", [](CommandContext&) {});

    const Command* found = registry.Find("noop");
    REQUIRE(found != nullptr);
    REQUIRE(found->Name() == "noop");
    REQUIRE(found->Docstring() == "Does nothing.");
}

TEST_CASE("Find returns nullptr for an unregistered name", "[Command]") {
    CommandRegistry registry;
    REQUIRE(registry.Find("does-not-exist") == nullptr);
}

TEST_CASE("Invoke runs the command against the given context", "[Command]") {
    CommandRegistry registry;
    registry.Register("insert-hello", "", [](CommandContext& context) {
        context.buffer.InsertAtPoint("hello");
    });

    Fixture fixture;
    CommandContext context = fixture.Context();
    registry.Invoke("insert-hello", context);

    REQUIRE(fixture.buffer.Text() == "hello");
}

TEST_CASE("Invoke throws for an unregistered name", "[Command]") {
    CommandRegistry registry;
    Fixture         fixture;
    CommandContext  context = fixture.Context();

    REQUIRE_THROWS_AS(registry.Invoke("nope", context), std::out_of_range);
}

TEST_CASE("Re-registering a name overwrites the previous command", "[Command]") {
    CommandRegistry registry;
    registry.Register("cmd", "first", [](CommandContext& context) { context.buffer.InsertAtPoint("A"); });
    registry.Register("cmd", "second", [](CommandContext& context) { context.buffer.InsertAtPoint("B"); });

    REQUIRE(registry.Find("cmd")->Docstring() == "second");

    Fixture fixture;
    CommandContext context = fixture.Context();
    registry.Invoke("cmd", context);
    REQUIRE(fixture.buffer.Text() == "B");
}

TEST_CASE("Names returns registered command names sorted", "[Command]") {
    CommandRegistry registry;
    registry.Register("zeta", "", [](CommandContext&) {});
    registry.Register("alpha", "", [](CommandContext&) {});
    registry.Register("mu", "", [](CommandContext&) {});

    REQUIRE(registry.Names() == std::vector<std::string>{"alpha", "mu", "zeta"});
}

TEST_CASE("CompleteCommandNames filters by prefix", "[Command]") {
    CommandRegistry registry;
    registry.Register("forward-char", "", [](CommandContext&) {});
    registry.Register("forward-word", "", [](CommandContext&) {});
    registry.Register("backward-char", "", [](CommandContext&) {});

    const auto matches = CompleteCommandNames(registry, "forward-");
    REQUIRE(matches == std::vector<std::string>{"forward-char", "forward-word"});

    REQUIRE(CompleteCommandNames(registry, "").size() == 3);
    REQUIRE(CompleteCommandNames(registry, "nothing-matches-this").empty());
}
