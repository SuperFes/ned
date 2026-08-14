#include <catch2/catch_test_macros.hpp>

#include <stdexcept>

#include "Editor/ScriptingSession.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "Text/KillRing.h"

using ned::editor::CommandContext;
using ned::editor::CommandContextScope;
using ned::editor::CommandRegistry;
using ned::editor::Keymap;
using ned::editor::ScriptingSession;
using ned::editor::ScriptingSessionScope;

namespace {

struct Fixture {
    ned::text::Buffer     buffer{"scratch"};
    ned::text::KillRing   killRing;
    ned::text::BufferList bufferList;
    CommandRegistry       registry;
    Keymap                scriptKeymap;

    CommandContext Context() {
        return CommandContext{buffer, killRing, bufferList};
    }
};

} // namespace

TEST_CASE("Current throws when no ScriptingSessionScope is active", "[ScriptingSession]") {
    REQUIRE_THROWS_AS(ScriptingSessionScope::Current(), std::runtime_error);
}

TEST_CASE("A ScriptingSessionScope makes its session current", "[ScriptingSession]") {
    Fixture fixture;
    ScriptingSessionScope scope(ScriptingSession{fixture.registry, fixture.scriptKeymap});

    ScriptingSession& current = ScriptingSessionScope::Current();
    REQUIRE(&current.registry == &fixture.registry);
    REQUIRE(&current.scriptKeymap == &fixture.scriptKeymap);
    REQUIRE(current.context == nullptr);
}

TEST_CASE("Nested ScriptingSessionScopes restore the outer one on destruction", "[ScriptingSession]") {
    Fixture outerFixture;
    Fixture innerFixture;

    ScriptingSessionScope outer(ScriptingSession{outerFixture.registry, outerFixture.scriptKeymap});
    REQUIRE(&ScriptingSessionScope::Current().registry == &outerFixture.registry);

    {
        ScriptingSessionScope inner(ScriptingSession{innerFixture.registry, innerFixture.scriptKeymap});
        REQUIRE(&ScriptingSessionScope::Current().registry == &innerFixture.registry);
    }

    REQUIRE(&ScriptingSessionScope::Current().registry == &outerFixture.registry);
}

TEST_CASE("CommandContextScope requires an active ScriptingSessionScope", "[ScriptingSession]") {
    Fixture         fixture;
    CommandContext  context = fixture.Context();
    REQUIRE_THROWS_AS(CommandContextScope(context), std::runtime_error);
}

TEST_CASE("CommandContextScope sets and restores the session's context pointer", "[ScriptingSession]") {
    Fixture fixture;
    ScriptingSessionScope session(ScriptingSession{fixture.registry, fixture.scriptKeymap});
    REQUIRE(ScriptingSessionScope::Current().context == nullptr);

    CommandContext context = fixture.Context();
    {
        CommandContextScope contextScope(context);
        REQUIRE(ScriptingSessionScope::Current().context == &context);
    }
    REQUIRE(ScriptingSessionScope::Current().context == nullptr);
}

TEST_CASE("Nested CommandContextScopes restore the outer context", "[ScriptingSession]") {
    Fixture fixtureA;
    Fixture fixtureB;
    ScriptingSessionScope session(ScriptingSession{fixtureA.registry, fixtureA.scriptKeymap});

    CommandContext contextA = fixtureA.Context();
    CommandContext contextB = fixtureB.Context();

    CommandContextScope outer(contextA);
    REQUIRE(ScriptingSessionScope::Current().context == &contextA);

    {
        CommandContextScope inner(contextB);
        REQUIRE(ScriptingSessionScope::Current().context == &contextB);
    }

    REQUIRE(ScriptingSessionScope::Current().context == &contextA);
}
