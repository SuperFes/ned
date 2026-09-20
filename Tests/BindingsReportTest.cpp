#include <catch2/catch_test_macros.hpp>

#include <string>

#include "Editor/BindingsReport.h"
#include "Editor/Command.h"
#include "Editor/Commands.h"
#include "Editor/Key.h"
#include "Editor/Keymap.h"

using ned::editor::BuildDefaultGlobalKeymap;
using ned::editor::CommandContext;
using ned::editor::CommandRegistry;
using ned::editor::Keymap;
using ned::editor::KeymapStack;
using ned::editor::ParseKeySequence;
using ned::editor::RegisterBuiltinCommands;
using ned::editor::RenderBindingsReport;

namespace {

CommandRegistry ThreeCommands() {
    CommandRegistry registry;
    registry.Register("save-buffer", "Write the current buffer to its file.", [](CommandContext&) {});
    registry.Register("compile", "Run the project's build command.", [](CommandContext&) {});
    registry.Register("lonely-command", "Never bound to anything.", [](CommandContext&) {});
    return registry;
}

} // namespace

TEST_CASE("The bindings report groups by layer and carries each docstring", "[BindingsReport]") {
    Keymap mode;
    Keymap global;
    mode.Bind(ParseKeySequence("C-c C-c"), "compile");
    global.Bind(ParseKeySequence("C-x C-s"), "save-buffer");

    const KeymapStack stack({&mode, &global}, {"Major mode", "Global"});
    const std::string report = RenderBindingsReport(stack, ThreeCommands(), "cpp");

    REQUIRE(report.find("major mode: cpp") != std::string::npos);
    REQUIRE(report.find("Major mode -- 1 binding\n") != std::string::npos);
    REQUIRE(report.find("Global -- 1 binding\n") != std::string::npos);
    REQUIRE(report.find("C-c C-c  compile") != std::string::npos);
    REQUIRE(report.find("Run the project's build command.") != std::string::npos);
    REQUIRE(report.find("C-x C-s  save-buffer") != std::string::npos);
}

TEST_CASE("The bindings report lists commands nothing reaches", "[BindingsReport]") {
    Keymap            global;
    const KeymapStack stack({&global}, {"Global"});
    const std::string report = RenderBindingsReport(stack, ThreeCommands(), "fundamental");

    REQUIRE(report.find("Unbound -- 3 commands reachable only from M-x") != std::string::npos);
    REQUIRE(report.find("  lonely-command\n") != std::string::npos);
}

TEST_CASE("The bindings report names what shadows an unreachable binding", "[BindingsReport]") {
    // The exact shape a hand-written init.janet produces: a user binding on
    // a prefix the global keymap still binds longer sequences under.
    Keymap user;
    Keymap global;
    user.Bind(ParseKeySequence("C-x"), "compile");
    global.Bind(ParseKeySequence("C-x C-s"), "save-buffer");

    const KeymapStack stack({&user, &global}, {"User (init.janet)", "Global"});
    const std::string report = RenderBindingsReport(stack, ThreeCommands(), "cpp");

    REQUIRE(report.find("Shadowed -- 1 binding that can never fire") != std::string::npos);
    REQUIRE(report.find("save-buffer -- Global layer, but compile fires instead") != std::string::npos);
    // The unreachable chord must not also appear as a live Global binding.
    REQUIRE(report.find("Global -- ") == std::string::npos);
}

TEST_CASE("The bindings report covers the shipped default keymap", "[BindingsReport]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);
    const Keymap      global = BuildDefaultGlobalKeymap();
    const KeymapStack stack({&global}, {"Global"});
    const std::string report = RenderBindingsReport(stack, registry, "fundamental");

    // Every row's command resolves -- a binding naming something nothing
    // registers is the failure CommandsTest guards against, and the report
    // would otherwise paper over it with its own placeholder.
    REQUIRE(report.find("(no such command") == std::string::npos);
    REQUIRE(report.find("C-x C-s") != std::string::npos);
    REQUIRE(report.find("C-c ?") != std::string::npos);
    REQUIRE(report.find("describe-bindings") != std::string::npos);
}

TEST_CASE("The bindings report collapses a printable-character run into a range", "[BindingsReport]") {
    CommandRegistry registry;
    registry.Register("self-insert-command", "Insert the character that was pressed.", [](CommandContext&) {});
    registry.Register("newline", "Insert a newline.", [](CommandContext&) {});

    Keymap global;
    for (char32_t codepoint = U' '; codepoint <= U'~'; ++codepoint) {
        global.Bind({ned::editor::KeyChord{.Codepoint = codepoint}}, "self-insert-command");
    }
    global.Bind(ParseKeySequence("RET"), "newline");

    const KeymapStack stack({&global}, {"Global"});
    const std::string report = RenderBindingsReport(stack, registry, "fundamental");

    // The heading still counts bindings, not rows.
    REQUIRE(report.find("Global -- 96 bindings") != std::string::npos);
    REQUIRE(report.find("SPC .. ~") != std::string::npos);
    REQUIRE(report.find("Insert the character that was pressed.") != std::string::npos);
    // One row for the whole run, so the docstring is emitted once.
    REQUIRE(report.find("Insert the character that was pressed.") ==
            report.rfind("Insert the character that was pressed."));
    // RET is not a plain character and keeps its own row.
    REQUIRE(report.find("RET") != std::string::npos);
    REQUIRE(report.find("newline") != std::string::npos);
}
