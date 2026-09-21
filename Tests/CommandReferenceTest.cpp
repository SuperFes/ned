#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "Editor/Command.h"
#include "Editor/Commands.h"
#include "Editor/Keymap.h"
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
fs::path CommandsManPath() {
    return fs::path(NED_REPO_ROOT) / "Docs" / "man" / "ned-commands.7.md";
}

// A man page's own rendering of the same list. Pandoc turns a definition list
// into the `.TP` blocks a reader expects; the version and footer are passed by
// CMake/ManPages.cmake at build time, so a version bump doesn't dirty this.
std::string RenderManHeader(std::string_view name, std::string_view section, std::string_view tagline) {
    std::ostringstream out;
    std::string        upper(name);
    std::ranges::transform(upper, upper.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    out << "% " << upper << "(" << section << ")\n\n"
        << "# NAME\n\n"
        << name << " - " << tagline << "\n\n";
    return out.str();
}

std::string Render(const ned::editor::CommandRegistry& registry) {
    // The default global keymap only. A major mode's own layer and anything
    // init.janet binds are both real and both absent here on purpose: this
    // page is generated from a static registry, and a per-mode key column
    // would be a different (per-language) page. describe-bindings is the
    // live answer -- see Editor/BindingsReport.h.
    const ned::editor::Keymap                globalKeymap = ned::editor::BuildDefaultGlobalKeymap();
    const ned::editor::KeymapStack           stack({&globalKeymap});
    const std::map<std::string, std::string> bindings = ned::editor::ShortestBindingPerCommand(stack);

    std::ostringstream out;
    out << "# Command reference\n\n"
        << "Every command reachable from `M-x`, from a keybinding, or from Janet via\n"
        << "`ned/run-command`.\n\n"
        << "The key shown is the shortest sequence bound in the default global keymap; a\n"
        << "command with none is reachable from `M-x` and Janet alone. Major-mode and\n"
        << "`init.janet` bindings are not listed here -- run `describe-bindings` (`C-c ?`)\n"
        << "for the live keymap stack, this page's own layer included.\n\n";

    const std::vector<std::string> names = registry.Names();
    out << names.size() << " commands.\n\n";

    for (const std::string& name : names) {
        const ned::editor::Command* command = registry.Find(name);
        if (command == nullptr) continue;
        out << "## `" << name << "`\n\n";
        const auto binding = bindings.find(name);
        if (binding != bindings.end()) {
            out << "Key: `" << binding->second << "`\n\n";
        }
        out << command->Docstring() << "\n\n";
    }
    return out.str();
}

std::string RenderCommandsMan(const ned::editor::CommandRegistry& registry) {
    const ned::editor::Keymap                globalKeymap = ned::editor::BuildDefaultGlobalKeymap();
    const ned::editor::KeymapStack           stack({&globalKeymap});
    const std::map<std::string, std::string> bindings = ned::editor::ShortestBindingPerCommand(stack);

    std::ostringstream out;
    out << RenderManHeader("ned-commands", "7", "every command ned(1) can run")
        << "# DESCRIPTION\n\n"
           "Every command reachable from `M-x`, from a keybinding, or from Janet via\n"
           "`ned/run-command`. The key shown is the shortest sequence bound in the default global\n"
           "keymap; a command with none is reachable from `M-x` and Janet alone. Major-mode and\n"
           "`init.janet` bindings are not listed -- run `describe-bindings` (`C-c ?`) inside ned\n"
           "for the live keymap stack.\n\n"
        << "# COMMANDS\n\n";

    for (const std::string& name : registry.Names()) {
        const ned::editor::Command* command = registry.Find(name);
        if (command == nullptr)
            continue;
        const auto binding = bindings.find(name);
        out << "`" << name << "`";
        if (binding != bindings.end())
            out << " (`" << binding->second << "`)";
        out << "\n\n:   " << command->Docstring() << "\n\n";
    }

    out << "# SEE ALSO\n\n**ned**(1), **ned-janet**(7)\n";
    return out.str();
}

std::string ReadFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in);
    std::ostringstream content;
    content << in.rdbuf();
    return content.str();
}

void CheckOrBless(const fs::path& path, const std::string& rendered) {
    if (std::getenv("NED_BLESS_COMMAND_DOCS") != nullptr) {
        fs::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        REQUIRE(out);
        out << rendered;
        SUCCEED("regenerated " + path.string() + " -- read the diff");
        return;
    }

    INFO("regenerate: NED_BLESS_COMMAND_DOCS=1 ./build/ned_tests \"[CommandDocs]\"");
    REQUIRE(fs::exists(path));
    CHECK(ReadFile(path) == rendered);
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
    CheckOrBless(ReferencePath(), Render(registry));
}

TEST_CASE("Docs/man/ned-commands.7.md matches the live registry", "[CommandDocs]") {
    ned::editor::CommandRegistry registry;
    ned::editor::RegisterBuiltinCommands(registry);
    CheckOrBless(CommandsManPath(), RenderCommandsMan(registry));
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
fs::path ScriptingManPath() {
    return fs::path(NED_REPO_ROOT) / "Docs" / "man" / "ned-janet.7.md";
}

std::string RenderBindings(const std::vector<std::pair<std::string, std::string>>& bindings) {
    std::ostringstream out;
    out << "# Scripting reference\n\n"
        << "Every `ned/*` function available to `init.janet`, a project's `.ned/init.janet`,\n"
        << "or a plugin.\n\n"
        << bindings.size() << " bindings.\n\n";
    for (const auto& [name, doc] : bindings) {
        out << "## `" << name << "`\n\n" << doc << "\n\n";
    }
    return out.str();
}

std::string RenderBindingsMan(const std::vector<std::pair<std::string, std::string>>& bindings) {
    std::ostringstream out;
    out << RenderManHeader("ned-janet", "7", "the Janet scripting API of ned(1)")
        << "# DESCRIPTION\n\n"
           "Every `ned/*` function available to `$XDG_CONFIG_HOME/ned/init.janet`, a project's\n"
           "`.ned/init.janet`, or a plugin. ned has no configuration file format of its own:\n"
           "configuration is Janet code, and these are the bindings it calls.\n\n"
        << "# BINDINGS\n\n";
    for (const auto& [name, doc] : bindings) {
        out << "`" << name << "`\n\n:   " << doc << "\n\n";
    }
    out << "# SEE ALSO\n\n**ned**(1), **ned-commands**(7)\n";
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

    CheckOrBless(ScriptingPath(), RenderBindings(bindings));
    CheckOrBless(ScriptingManPath(), RenderBindingsMan(bindings));
}
