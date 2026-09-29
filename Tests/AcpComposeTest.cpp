//
// The ACP compose buffer: opened by the panel (C-c '), finished with
// C-c C-c / C-c C-k in its own keymap-only mode.
//

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>

#include "Editor/Acp/Compose.h"
#include "Editor/Commands.h"
#include "Editor/Dispatcher.h"
#include "Editor/Keymap.h"
#include "Editor/Mode.h"
#include "Editor/ModeOverrides.h"
#include "Editor/PromptHistory.h"
#include "Editor/Register.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "Text/KillRing.h"
#include "UI/ActiveBuffer.h"
#include "UI/BufferView.h"
#include "UI/Theme.h"

namespace {

struct Fixture {
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
    ned::text::Buffer&    original = bufferList.OpenOrCreateFile("/repo/original.txt");
    ned::ui::ActiveBuffer activeBuffer{original};
    ned::ui::BufferView   view{activeBuffer, killRing, registers, promptHistory, bufferList, dispatcher, statusMessage, mode, theme};

    std::optional<std::string> sent;
    bool                       cancelled = false;

    ned::editor::acp::ComposeCallbacks Callbacks() {
        return {.onSend = [this](std::string text) { sent = std::move(text); }, .onCancel = [this] { cancelled = true; }};
    }
};

} // namespace

TEST_CASE("The ACP compose buffer opens seeded, in its own mode binding C-c C-c and C-c C-k", "[Acp][AcpCompose]") {
    Fixture fixture;
    fixture.view.BeginAcpCompose("draft", fixture.Callbacks());

    ned::text::Buffer& compose = fixture.activeBuffer.Get();
    REQUIRE(compose.Name() == ned::editor::acp::kComposeBufferName);
    REQUIRE(compose.Text() == "draft");
    const ned::editor::Mode mode = ned::editor::CachedModeForBuffer(compose);
    REQUIRE(mode.name == ned::editor::acp::kComposeModeName);
    REQUIRE(mode.wrapLines);
    REQUIRE(mode.keymap.Resolve(ned::editor::ParseKeySequence("C-c C-c")).commandName == "acp-compose-finish");
    REQUIRE(mode.keymap.Resolve(ned::editor::ParseKeySequence("C-c C-k")).commandName == "acp-compose-abort");
}

TEST_CASE("Finishing the ACP compose buffer closes it and sends its text, trailing blank lines trimmed", "[Acp][AcpCompose]") {
    Fixture fixture;
    fixture.view.BeginAcpCompose("draft", fixture.Callbacks());
    fixture.activeBuffer.Get().InsertAtPoint("\nmore\n\n");

    fixture.view.FinishAcpCompose(true);
    REQUIRE(fixture.sent == "draft\nmore");
    REQUIRE(&fixture.activeBuffer.Get() == &fixture.original);
    REQUIRE(fixture.bufferList.Find(std::string(ned::editor::acp::kComposeBufferName)) == nullptr);
}

TEST_CASE("Cancelling the ACP compose buffer closes it without sending", "[Acp][AcpCompose]") {
    Fixture fixture;
    fixture.view.BeginAcpCompose("draft", fixture.Callbacks());
    fixture.view.FinishAcpCompose(false);
    REQUIRE(fixture.cancelled);
    REQUIRE_FALSE(fixture.sent);
}

TEST_CASE("acp-compose-finish outside a compose buffer only reports it", "[Acp][AcpCompose]") {
    Fixture fixture;
    fixture.view.FinishAcpCompose(true);
    REQUIRE(fixture.statusMessage == "Not an ACP compose buffer.");
    REQUIRE(&fixture.activeBuffer.Get() == &fixture.original);
}
