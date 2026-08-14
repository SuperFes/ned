#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <string>

#include "Editor/Dispatcher.h"
#include "Editor/FormatOnSave.h"
#include "Editor/Key.h"
#include "Editor/Keymap.h"
#include "Editor/ProjectRoot.h"
#include "Editor/ScratchPad.h"
#include "Editor/ScriptingSession.h"
#include "Janet/EditorBindings.h"
#include "Janet/Environment.h"
#include "Janet/Value.h"
#include "JanetTestSupport.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "Text/KillRing.h"

using ned::janet::Environment;
using ned::janet::FromJanet;
using ned::janet::InstallEditorBindings;

namespace {

struct Fixture {
    ned::text::Buffer            buffer{"scratch"};
    ned::text::KillRing          killRing;
    ned::text::BufferList        bufferList;
    ned::editor::CommandRegistry registry;
    ned::editor::Keymap          scriptKeymap;

    ned::editor::CommandContext Context() {
        return ned::editor::CommandContext{buffer, killRing, bufferList};
    }
};

} // namespace

TEST_CASE("ned/insert and point bindings operate on the active CommandContext", "[EditorBindings]") {
    Environment& env = ned_tests::TestEnvironment();
    InstallEditorBindings(env);

    Fixture                            fixture;
    ned::editor::ScriptingSessionScope session(ned::editor::ScriptingSession{fixture.registry, fixture.scriptKeymap});
    ned::editor::CommandContext        context = fixture.Context();
    ned::editor::CommandContextScope   contextScope(context);

    env.DoString(R"((ned/insert "hello"))");
    REQUIRE(fixture.buffer.Text() == "hello");
    REQUIRE(FromJanet<std::size_t>(env.DoString("(ned/point)")) == 5);

    env.DoString("(ned/backward-char)");
    REQUIRE(fixture.buffer.Point() == 4);

    REQUIRE(FromJanet<std::string>(env.DoString("(ned/buffer-text)")) == "hello");

    env.DoString("(ned/delete-char)");
    REQUIRE(fixture.buffer.Text() == "hell");

    env.DoString("(ned/backward-delete-char)");
    REQUIRE(fixture.buffer.Text() == "hel");
}

TEST_CASE("ned/message writes into the active CommandContext's message sink", "[EditorBindings]") {
    Environment& env = ned_tests::TestEnvironment();
    InstallEditorBindings(env);

    Fixture                            fixture;
    ned::editor::ScriptingSessionScope session(ned::editor::ScriptingSession{fixture.registry, fixture.scriptKeymap});

    std::string                 message;
    ned::editor::CommandContext context = fixture.Context();
    context.message                     = &message;
    ned::editor::CommandContextScope contextScope(context);

    env.DoString(R"((ned/message "hello from janet"))");
    REQUIRE(message == "hello from janet");
}

TEST_CASE("ned/* functions error when no command context is active", "[EditorBindings]") {
    Environment& env = ned_tests::TestEnvironment();
    InstallEditorBindings(env);

    Fixture                            fixture;
    ned::editor::ScriptingSessionScope session(ned::editor::ScriptingSession{fixture.registry, fixture.scriptKeymap});
    // deliberately no CommandContextScope active

    REQUIRE_THROWS_AS(env.DoString("(ned/point)"), std::runtime_error);
    REQUIRE_THROWS_AS(env.DoString(R"((ned/insert "x"))"), std::runtime_error);
}

TEST_CASE("ned/register-command makes a Janet function invocable through CommandRegistry", "[EditorBindings]") {
    Environment& env = ned_tests::TestEnvironment();
    InstallEditorBindings(env);

    Fixture                            fixture;
    ned::editor::ScriptingSessionScope session(ned::editor::ScriptingSession{fixture.registry, fixture.scriptKeymap});

    env.DoString(R"(
        (ned/register-command "janet-greet" "Greets."
          (fn [] (ned/insert "hi")))
    )");

    REQUIRE(fixture.registry.Find("janet-greet") != nullptr);

    ned::editor::CommandContext context = fixture.Context();
    fixture.registry.Invoke("janet-greet", context);

    REQUIRE(fixture.buffer.Text() == "hi");
}

TEST_CASE("Re-registering a Janet command under the same name replaces its behavior", "[EditorBindings]") {
    Environment& env = ned_tests::TestEnvironment();
    InstallEditorBindings(env);

    Fixture                            fixture;
    ned::editor::ScriptingSessionScope session(ned::editor::ScriptingSession{fixture.registry, fixture.scriptKeymap});

    env.DoString(R"((ned/register-command "redef-test" "" (fn [] (ned/insert "A"))))");
    env.DoString(R"((ned/register-command "redef-test" "" (fn [] (ned/insert "B"))))");

    ned::editor::CommandContext context = fixture.Context();
    fixture.registry.Invoke("redef-test", context);

    REQUIRE(fixture.buffer.Text() == "B");
}

TEST_CASE("Janet code can define a new command, bind it to a key, and a real Dispatcher fires it", "[EditorBindings]") {
    // The litmus test for "everything programmable": Janet defines a brand
    // new interactive command and binds it to a key sequence, with no C++
    // code involved beyond installing the bindings -- then a real Dispatcher,
    // fed real key chords, actually runs it and mutates a real buffer.
    Environment& env = ned_tests::TestEnvironment();
    InstallEditorBindings(env);

    Fixture                            fixture;
    ned::editor::ScriptingSessionScope session(ned::editor::ScriptingSession{fixture.registry, fixture.scriptKeymap});

    env.DoString(R"(
        (ned/register-command "insert-greeting" "Insert a greeting."
          (fn [] (ned/insert "hello from janet")))
        (ned/define-key "C-c C-j" "insert-greeting")
    )");

    const ned::editor::KeymapStack keymaps({&fixture.scriptKeymap});
    ned::editor::Dispatcher        dispatcher(fixture.registry, keymaps);
    ned::editor::CommandContext    context = fixture.Context();

    using ned::editor::ParseKeyChord;
    REQUIRE(dispatcher.Feed(ParseKeyChord("C-c"), context) == ned::editor::Dispatcher::Outcome::Pending);
    REQUIRE(dispatcher.Feed(ParseKeyChord("C-j"), context) == ned::editor::Dispatcher::Outcome::Invoked);

    REQUIRE(fixture.buffer.Text() == "hello from janet");
}

TEST_CASE("ned/set-format-command configures the process-wide format command", "[EditorBindings]") {
    // FormatCommand is process-wide state (see FormatOnSave.h); guaranteed
    // reset via RAII so this doesn't leak into other tests.
    struct FormatCommandGuard {
        ~FormatCommandGuard() {
            ned::editor::SetFormatCommand(std::nullopt);
        }
    } guard;

    Environment& env = ned_tests::TestEnvironment();
    InstallEditorBindings(env);

    env.DoString(R"((ned/set-format-command "clang-format"))");
    REQUIRE(ned::editor::FormatCommand() == std::optional<std::string>("clang-format"));

    env.DoString(R"((ned/set-format-command ""))"); // empty string clears it
    REQUIRE_FALSE(ned::editor::FormatCommand().has_value());
}

TEST_CASE("ned/set-auto-detect-project-root configures the process-wide toggle", "[EditorBindings]") {
    // AutoDetectProjectRoot is process-wide state (see ProjectRoot.h);
    // guaranteed reset via RAII so this doesn't leak into other tests.
    struct AutoDetectGuard {
        ~AutoDetectGuard() {
            ned::editor::SetAutoDetectProjectRoot(true);
        }
    } guard;

    Environment& env = ned_tests::TestEnvironment();
    InstallEditorBindings(env);

    env.DoString(R"((ned/set-auto-detect-project-root false))");
    REQUIRE_FALSE(ned::editor::AutoDetectProjectRoot());

    env.DoString(R"((ned/set-auto-detect-project-root true))");
    REQUIRE(ned::editor::AutoDetectProjectRoot());
}

TEST_CASE("ned/set-scratch-auto-save configures the process-wide toggle", "[EditorBindings]") {
    // ScratchAutoSaveEnabled is process-wide state (see ScratchPad.h);
    // guaranteed reset via RAII so this doesn't leak into other tests.
    struct AutoSaveGuard {
        ~AutoSaveGuard() {
            ned::editor::SetScratchAutoSaveEnabled(true);
        }
    } guard;

    Environment& env = ned_tests::TestEnvironment();
    InstallEditorBindings(env);

    env.DoString(R"((ned/set-scratch-auto-save false))");
    REQUIRE_FALSE(ned::editor::ScratchAutoSaveEnabled());

    env.DoString(R"((ned/set-scratch-auto-save true))");
    REQUIRE(ned::editor::ScratchAutoSaveEnabled());
}
