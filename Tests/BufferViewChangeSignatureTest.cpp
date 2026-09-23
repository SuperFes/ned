//
// change-signature end to end: the command (Editor/Commands.cpp) through
// BufferView's own prompt, project-wide discovery and the *signature*
// review's commit. The planner's own arithmetic is covered purely in
// ChangeSignatureTest.cpp, the query captures in ChangeSignatureQueryTest.cpp
// and discovery's own aggregation in ChangeSignatureDiscoveryTest.cpp; what
// this file pins is the wiring -- that M-x change-signature resolves the
// function at point with no keybinding at all, that committing the review
// rewrites both the definition and a call site in a DIFFERENT file, and that
// a decline surfaces as a status message with no review opened.
//

#include <catch2/catch_test_macros.hpp>

#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "Editor/ChangeSignatureSettings.h"
#include "Editor/Commands.h"
#include "Editor/Dispatcher.h"
#include "Editor/Keymap.h"
#include "Editor/Mode.h"
#include "Editor/ModeOverrides.h"
#include "Editor/Multibuffer.h"
#include "Editor/Project/Root.h"
#include "Editor/PromptHistory.h"
#include "Editor/Register.h"
#include "TestEvents.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "Text/KillRing.h"
#include "UI/ActiveBuffer.h"
#include "UI/BufferView.h"
#include "UI/Theme.h"

using ned::text::Buffer;
using ned::ui::BufferView;
namespace test = ned::ui::test;

namespace {

struct RegistryResetGuard {
    RegistryResetGuard() {
        ned::editor::multibuffer::ClearRegistryForTesting();
    }
    ~RegistryResetGuard() {
        ned::editor::multibuffer::ClearRegistryForTesting();
    }
};

struct ProjectRootResetGuard {
    std::filesystem::path saved = ned::editor::ProjectRoot();
    ~ProjectRootResetGuard() {
        ned::editor::SetProjectRoot(saved);
    }
};

std::size_t NextFixtureId() {
    static std::size_t next = 0;
    return next++;
}

// A two-file C++ project on disk: Widget.cpp defines add(), Caller.cpp
// calls it once -- real files, so the project-wide candidate search finds
// Caller.cpp the same way it would in a real tree.
struct Fixture {
    RegistryResetGuard    registryResetGuard;
    ProjectRootResetGuard rootGuard;
    std::filesystem::path dir = std::filesystem::temp_directory_path() /
                                ("ned_change_signature_test_" + std::to_string(::getpid()) + "_" +
                                 std::to_string(NextFixtureId()));

    ned::text::KillRing          killRing;
    ned::editor::RegisterTable   registers;
    ned::editor::PromptHistory   promptHistory;
    ned::text::BufferList        bufferList;
    ned::editor::CommandRegistry registry{[] {
        ned::editor::CommandRegistry r;
        ned::editor::RegisterBuiltinCommands(r);
        return r;
    }()};
    ned::editor::Keymap          keymap = ned::editor::BuildDefaultGlobalKeymap();
    ned::editor::Dispatcher      dispatcher{registry, ned::editor::KeymapStack({&keymap})};
    ned::editor::Mode            mode  = ned::editor::FundamentalMode();
    ned::ui::Theme               theme = ned::ui::DarkTheme();

    std::string                          statusMessage;
    Buffer*                              widget = nullptr;
    Buffer*                              caller = nullptr;
    std::optional<ned::ui::ActiveBuffer> activeBuffer;

    Fixture() {
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        std::ofstream(dir / "Widget.cpp") << "int add(int a, int b) {\n    return a + b;\n}\n";
        std::ofstream(dir / "Caller.cpp") << "void run() {\n    add(1, 2);\n}\n";

        ned::editor::SetProjectRoot(dir);
        mode   = ned::editor::ModeForPath(dir / "Widget.cpp");
        widget = &bufferList.OpenOrCreateFile(dir / "Widget.cpp");
        caller = &bufferList.OpenOrCreateFile(dir / "Caller.cpp");
        activeBuffer.emplace(*widget);
    }

    ~Fixture() {
        std::filesystem::remove_all(dir);
    }

    BufferView View() {
        return BufferView(*activeBuffer, killRing, registers, promptHistory, bufferList, dispatcher, statusMessage,
                          mode, theme);
    }
};

std::string Content(const Buffer& buffer) {
    return buffer.Content().Substring(0, buffer.Content().ByteLength());
}

void Type(BufferView& view, const std::string& text) {
    for (const char c : text) {
        view.OnEvent(test::Character(std::string(1, c)));
    }
}

// M-x <name> <Enter> -- the only way to reach change-signature, which
// carries no keybinding of its own (ROADMAP-scoped: reachable by name only).
void InvokeCommand(BufferView& view, std::string_view name) {
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 20});
    view.OnEvent(test::Alt('x'));
    Type(view, std::string(name));
    view.OnEvent(test::Return());
}

// Replaces the prompt's prefilled text (its cursor sits at the end after a
// SetText prefill, same as rename-symbol's own prompt) with `text`.
void ReplacePromptText(BufferView& view, std::size_t prefillLength, const std::string& text) {
    for (std::size_t i = 0; i < prefillLength; ++i) {
        view.OnEvent(test::Backspace());
    }
    Type(view, text);
}

} // namespace

TEST_CASE("change-signature reorders parameters and rewrites a call site in another file",
          "[BufferView][ChangeSignature]") {
    Fixture     fixture;
    BufferView  view = fixture.View();
    fixture.widget->SetPoint(Content(*fixture.widget).find("int a"));

    InvokeCommand(view, "change-signature");
    INFO("status: " << fixture.statusMessage);
    REQUIRE(fixture.statusMessage.find("New signature:") != std::string::npos);
    REQUIRE(fixture.statusMessage.find("int a, int b") != std::string::npos);

    ReplacePromptText(view, std::string("int a, int b").size(), "int b, int a");
    view.OnEvent(test::Return());

    INFO("status after commit prompt: " << fixture.statusMessage);
    Buffer* const review = fixture.bufferList.Find("*signature*");
    REQUIRE(review != nullptr);
    REQUIRE(&fixture.activeBuffer->Get() == review);
    CHECK(fixture.statusMessage.find("2 edits in 2 files") != std::string::npos);

    view.OnEvent(test::Ctrl('c'));
    view.OnEvent(test::Ctrl('c'));
    view.OnEvent(test::Character("b")); // into the open buffers

    CHECK(Content(*fixture.widget) == "int add(int b, int a) {\n    return a + b;\n}\n");
    CHECK(Content(*fixture.caller) == "void run() {\n    add(2, 1);\n}\n");
}

TEST_CASE("change-signature declines a new parameter with no default and opens no review",
          "[BufferView][ChangeSignature]") {
    Fixture     fixture;
    BufferView  view = fixture.View();
    fixture.widget->SetPoint(Content(*fixture.widget).find("int a"));

    InvokeCommand(view, "change-signature");
    REQUIRE(fixture.statusMessage.find("New signature:") != std::string::npos);

    ReplacePromptText(view, std::string("int a, int b").size(), "int a, int b, bool flag");
    view.OnEvent(test::Return());

    INFO("status: " << fixture.statusMessage);
    CHECK(fixture.bufferList.Find("*signature*") == nullptr);
    CHECK(fixture.statusMessage.find("flag") != std::string::npos);
    CHECK(fixture.statusMessage.find("default") != std::string::npos);
    CHECK(Content(*fixture.widget) == "int add(int a, int b) {\n    return a + b;\n}\n");
}

TEST_CASE("change-signature adds a defaulted parameter, using the default at the call site",
          "[BufferView][ChangeSignature]") {
    Fixture     fixture;
    BufferView  view = fixture.View();
    fixture.widget->SetPoint(Content(*fixture.widget).find("int a"));

    InvokeCommand(view, "change-signature");
    ReplacePromptText(view, std::string("int a, int b").size(), "int a, int b, bool negate = false");
    view.OnEvent(test::Return());

    REQUIRE(fixture.bufferList.Find("*signature*") != nullptr);
    view.OnEvent(test::Ctrl('c'));
    view.OnEvent(test::Ctrl('c'));
    view.OnEvent(test::Character("b"));

    CHECK(Content(*fixture.widget) == "int add(int a, int b, bool negate = false) {\n    return a + b;\n}\n");
    CHECK(Content(*fixture.caller) == "void run() {\n    add(1, 2, false);\n}\n");
}

TEST_CASE("ned/set-change-signature-max-files round-trips", "[ChangeSignature]") {
    const std::size_t previous = ned::editor::ChangeSignatureMaxFiles();
    ned::editor::SetChangeSignatureMaxFiles(1);
    CHECK(ned::editor::ChangeSignatureMaxFiles() == 1);
    ned::editor::SetChangeSignatureMaxFiles(previous);
}
