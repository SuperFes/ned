//
// The numbered-choice list every "pick one of these" prompt shares
// (BufferView/ChoicePrompt.h), driven through the ACP permission request --
// the one such prompt with a public entry point, so a test can put one up
// without reaching into BufferView's internals.
//

#include <catch2/catch_test_macros.hpp>

#include <string>

#include "Editor/Acp/Manager.h"
#include "Editor/Commands.h"
#include "Editor/Dispatcher.h"
#include "Editor/Keymap.h"
#include "Editor/Mode.h"
#include "Editor/PromptHistory.h"
#include "Editor/Register.h"
#include "TestEvents.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "Text/KillRing.h"
#include "UI/ActiveBuffer.h"
#include "UI/BufferView.h"
#include "UI/Theme.h"

using ned::ui::BufferView;

namespace {

struct Fixture {
    ned::text::Buffer          buffer{"scratch"};
    ned::text::KillRing        killRing;
    ned::editor::RegisterTable registers;
    ned::editor::PromptHistory promptHistory;
    ned::text::BufferList      bufferList;

    ned::editor::CommandRegistry registry{[] {
        ned::editor::CommandRegistry r;
        ned::editor::RegisterBuiltinCommands(r);
        return r;
    }()};
    ned::editor::Keymap          keymap = ned::editor::BuildDefaultGlobalKeymap();
    ned::editor::Dispatcher      dispatcher{registry, ned::editor::KeymapStack({&keymap})};
    ned::editor::Mode            mode  = ned::editor::FundamentalMode();
    ned::ui::Theme               theme = ned::ui::DarkTheme();

    std::string           statusMessage;
    ned::ui::ActiveBuffer activeBuffer{buffer};

    BufferView View() {
        return BufferView(activeBuffer, killRing, registers, promptHistory, bufferList, dispatcher, statusMessage,
                          mode, theme);
    }
};

// Three options, so a digit past the end has somewhere obvious to go wrong.
ned::editor::acp::Manager::PermissionPrompt ThreeOptions() {
    ned::editor::acp::Manager::PermissionPrompt prompt;
    prompt.description = "Run command";
    prompt.options     = {{.optionId = "once", .name = "Allow once"},
                          {.optionId = "always", .name = "Allow always"},
                          {.optionId = "deny", .name = "Deny"}};
    return prompt;
}

} // namespace

TEST_CASE("The first entry starts highlighted", "[BufferView][ChoicePrompt]") {
    Fixture    fixture;
    BufferView view = fixture.View();
    view.ShowAcpPermissionPrompt(ThreeOptions());

    REQUIRE(fixture.statusMessage.find("[1) Allow once]") != std::string::npos);
}

TEST_CASE("Down and Up move the highlight and wrap at both ends", "[BufferView][ChoicePrompt]") {
    Fixture    fixture;
    BufferView view = fixture.View();
    view.ShowAcpPermissionPrompt(ThreeOptions());

    view.OnEvent(ned::ui::test::ArrowDown());
    REQUIRE(fixture.statusMessage.find("[2) Allow always]") != std::string::npos);

    view.OnEvent(ned::ui::test::ArrowUp());
    view.OnEvent(ned::ui::test::ArrowUp());
    REQUIRE(fixture.statusMessage.find("[3) Deny]") != std::string::npos); // wrapped off the top
}

TEST_CASE("A digit picks that entry and commits it", "[BufferView][ChoicePrompt]") {
    Fixture    fixture;
    BufferView view = fixture.View();
    view.ShowAcpPermissionPrompt(ThreeOptions());

    view.OnEvent(ned::ui::test::Character('3'));

    REQUIRE(fixture.statusMessage == "Selected \"Deny\".");
}

TEST_CASE("Enter commits whichever entry is highlighted", "[BufferView][ChoicePrompt]") {
    Fixture    fixture;
    BufferView view = fixture.View();
    view.ShowAcpPermissionPrompt(ThreeOptions());

    view.OnEvent(ned::ui::test::ArrowDown());
    view.OnEvent(ned::ui::test::Return());

    REQUIRE(fixture.statusMessage == "Selected \"Allow always\".");
}

TEST_CASE("A digit with no entry behind it commits nothing", "[BufferView][ChoicePrompt]") {
    Fixture    fixture;
    BufferView view = fixture.View();
    view.ShowAcpPermissionPrompt(ThreeOptions());

    // Three options, so 7 names nothing. Two of these prompts used to fall
    // through here and commit whatever happened to be highlighted -- answering
    // a permission request with an option the user never chose.
    view.OnEvent(ned::ui::test::Character('7'));

    REQUIRE(fixture.statusMessage.find("Selected") == std::string::npos);
    REQUIRE(fixture.statusMessage.find("[1) Allow once]") != std::string::npos); // still up, still on the first
}

TEST_CASE("An unrelated key leaves the list alone", "[BufferView][ChoicePrompt]") {
    Fixture    fixture;
    BufferView view = fixture.View();
    view.ShowAcpPermissionPrompt(ThreeOptions());

    view.OnEvent(ned::ui::test::Character('q'));

    REQUIRE(fixture.statusMessage.find("Selected") == std::string::npos);
    REQUIRE(fixture.statusMessage.find("[1) Allow once]") != std::string::npos);
}

TEST_CASE("The quit chord cancels without committing", "[BufferView][ChoicePrompt]") {
    Fixture    fixture;
    BufferView view = fixture.View();
    view.ShowAcpPermissionPrompt(ThreeOptions());

    view.OnEvent(ned::ui::test::Escape());

    REQUIRE(fixture.statusMessage == "Permission request dismissed.");
}
