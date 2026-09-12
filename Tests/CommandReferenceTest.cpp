#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "Editor/Command.h"
#include "Editor/Commands.h"
#include "Janet/EditorBindings.h"
#include "Janet/Environment.h"
#include "JanetTestSupport.h"

#include <utility>

// Docs/Commands.md, generated from the live CommandRegistry.
//
// Docs/FormattingCapabilities.md's companion problem: ned has 275 registered
// commands, every one of them carrying a docstring that was written at
// registration and then seen by nobody -- M-x shows it, and that is all. The
// README is 70 lines. This turns an asset the codebase already pays for into
// the reference page.
//
// Generated rather than written, and held against the registry on every run,
// because a hand-maintained list of 275 commands is a list that is wrong. That
// was the whole argument for doing it this way (ROADMAP's "Generate the
// command/binding reference rather than writing it"), so the guard is the
// feature, not an extra.
//
//     NED_BLESS_COMMAND_DOCS=1 ./build/ned_tests "[CommandDocs]"
//
// then read the diff. A new command with no docstring fails the build: an
// undocumented command is undiscoverable, and M-x will show the blank too.

namespace {

namespace fs = std::filesystem;

fs::path ReferencePath() { return fs::path(NED_REPO_ROOT) / "Docs" / "Commands.md"; }

std::string Render(const ned::editor::CommandRegistry& registry) {
    std::ostringstream out;
    out << "# Command reference\n\n"
        << "Every command reachable from `M-x`, from a keybinding, or from Janet via\n"
        << "`ned/run-command`. **Generated** from the live `CommandRegistry` -- edit the\n"
        << "docstring at the registration site, not this file.\n\n"
        << "Regenerate with `NED_BLESS_COMMAND_DOCS=1 ./build/ned_tests \"[CommandDocs]\"`.\n"
        << "`Tests/CommandReferenceTest.cpp` holds this against the registry on every\n"
        << "build, so it cannot drift and a command cannot arrive undocumented.\n\n";

    const std::vector<std::string> names = registry.Names();
    out << names.size() << " commands.\n\n";

    for (const std::string& name : names) {
        const ned::editor::Command* command = registry.Find(name);
        if (command == nullptr) continue;
        out << "## `" << name << "`\n\n" << command->Docstring() << "\n\n";
    }
    return out.str();
}

std::string ReadFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in);
    std::ostringstream content;
    content << in.rdbuf();
    return content.str();
}

} // namespace

TEST_CASE("Every registered command carries a docstring", "[CommandDocs]") {
    // Checked before the reference is generated, because an empty entry in a
    // generated file reads as a formatting bug rather than a missing docstring.
    ned::editor::CommandRegistry registry;
    ned::editor::RegisterBuiltinCommands(registry);

    std::vector<std::string> undocumented;
    for (const std::string& name : registry.Names()) {
        const ned::editor::Command* command = registry.Find(name);
        REQUIRE(command != nullptr);
        if (command->Docstring().empty()) undocumented.push_back(name);
    }

    std::string joined;
    for (const std::string& name : undocumented) joined += " " + name;
    INFO("commands with no docstring:" << joined);
    CHECK(undocumented.empty());
}

TEST_CASE("Docs/Commands.md matches the live registry", "[CommandDocs]") {
    ned::editor::CommandRegistry registry;
    ned::editor::RegisterBuiltinCommands(registry);
    const std::string rendered = Render(registry);

    if (std::getenv("NED_BLESS_COMMAND_DOCS") != nullptr) {
        std::ofstream out(ReferencePath(), std::ios::binary | std::ios::trunc);
        REQUIRE(out);
        out << rendered;
        SUCCEED("regenerated Docs/Commands.md -- read the diff");
        return;
    }

    INFO("regenerate: NED_BLESS_COMMAND_DOCS=1 ./build/ned_tests \"[CommandDocs]\"");
    REQUIRE(fs::exists(ReferencePath()));
    CHECK(ReadFile(ReferencePath()) == rendered);
}

// The other half: Docs/Scripting.md, from the live `ned/*` binding table.
//
// Same argument as the commands above. 159 bindings, every one carrying a
// docstring that Janet keeps as `:doc` and that until now only surfaced to
// someone already sitting in a REPL typing `(doc ned/set-tab-width)`.
//
//     NED_BLESS_COMMAND_DOCS=1 ./build/ned_tests "[CommandDocs]"

namespace {

fs::path ScriptingPath() { return fs::path(NED_REPO_ROOT) / "Docs" / "Scripting.md"; }

std::string RenderBindings(const std::vector<std::pair<std::string, std::string>>& bindings) {
    std::ostringstream out;
    out << "# Scripting reference\n\n"
        << "Every `ned/*` function available to `init.janet`, a project's `.ned/init.janet`,\n"
        << "or a plugin. **Generated** from the live binding table -- edit the docstring at\n"
        << "the `Register<Fn>` call site, not this file.\n\n"
        << "Regenerate with `NED_BLESS_COMMAND_DOCS=1 ./build/ned_tests \"[CommandDocs]\"`.\n"
        << "Held against the binding table on every build, so it cannot drift.\n\n"
        << bindings.size() << " bindings.\n\n";
    for (const auto& [name, doc] : bindings) {
        out << "## `" << name << "`\n\n" << doc << "\n\n";
    }
    return out.str();
}

} // namespace

TEST_CASE("Docs/Scripting.md matches the live ned/* binding table", "[CommandDocs][Janet]") {
    ned::janet::Environment& environment = ned_tests::TestEnvironment();
    ned::janet::InstallEditorBindings(environment);

    // RegisteredBindings, not BindingDocsWithPrefix: the Janet environment is
    // shared across the whole single-process test run and other tests define
    // their own ned/* symbols in it (a VCS provider fixture, a command-redef
    // case). Reading the environment counted those and the generated page
    // disagreed with itself depending on test order -- caught by
    // ./build/ned_tests, which ctest -j8 cannot see, which is exactly why both
    // are run.
    const auto bindings = environment.RegisteredBindings("ned/");
    REQUIRE_FALSE(bindings.empty());

    // An undocumented binding is undiscoverable -- `(doc ned/whatever)` shows
    // the blank too, so this is not only about the generated page.
    std::string undocumented;
    for (const auto& [name, doc] : bindings) {
        if (doc.empty()) undocumented += " " + name;
    }
    INFO("bindings with no docstring:" << undocumented);
    CHECK(undocumented.empty());

    const std::string rendered = RenderBindings(bindings);
    if (std::getenv("NED_BLESS_COMMAND_DOCS") != nullptr) {
        std::ofstream out(ScriptingPath(), std::ios::binary | std::ios::trunc);
        REQUIRE(out);
        out << rendered;
        SUCCEED("regenerated Docs/Scripting.md -- read the diff");
        return;
    }

    INFO("regenerate: NED_BLESS_COMMAND_DOCS=1 ./build/ned_tests \"[CommandDocs]\"");
    REQUIRE(fs::exists(ScriptingPath()));
    CHECK(ReadFile(ScriptingPath()) == rendered);
}
