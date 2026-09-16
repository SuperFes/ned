//
// case-kind follow-up: fix-case-violation-at-point end to end -- the
// "fixer" ROADMAP.md recorded as still-open once Editor/FormatCase.h's
// checker landed. What this file pins is the wiring: M-x resolves the
// command, the violation nearest point is found, and rename-symbol's
// existing local-fast-path prompt opens pre-filled with
// SuggestNameForConvention's own suggestion rather than the violating name
// -- Enter alone (no further typing) is what applies it, confirming this
// is still a user-confirmed rename, never a silent rewrite.
//
// The LSP-tier prefill override (Lsp.cpp's RequestPrepareRenameAtPoint) is
// NOT covered here -- it shares the same one-line consume-before-use
// pattern the local tier below already exercises, and stubbing a fake LSP
// client's prepareRename response is out of scope for this pass.
//

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>

#include "Editor/Commands.h"
#include "Editor/Dispatcher.h"
#include "Editor/FormatRules.h"
#include "Editor/Keymap.h"
#include "Editor/Mode.h"
#include "Editor/ModeOverrides.h"
#include "Editor/PromptHistory.h"
#include "Editor/Register.h"
#include "TestEvents.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "Text/KillRing.h"
#include "UI/ActiveBuffer.h"
#include "UI/BufferView.h"
#include "UI/Theme.h"

using ned::editor::CaseConvention;
using ned::editor::SetCaseConvention;
using ned::ui::BufferView;
namespace test = ned::ui::test;

namespace {

struct FormatRulesGuard {
    ~FormatRulesGuard() {
        SetCaseConvention("parameter", std::nullopt);
        SetCaseConvention("local", std::nullopt);
    }
};

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
    ned::editor::Mode            mode  = *ned::editor::ModeByName("cpp-mode");
    ned::ui::Theme               theme = ned::ui::DarkTheme();

    std::string           statusMessage;
    ned::ui::ActiveBuffer activeBuffer{buffer};

    explicit Fixture(const std::string& text) {
        buffer.InsertAtPoint(text);
    }

    BufferView View() {
        return BufferView(activeBuffer, killRing, registers, promptHistory, bufferList, dispatcher, statusMessage,
                          mode, theme);
    }
};

void Type(BufferView& view, const std::string& text) {
    for (const char ch : text) {
        view.OnEvent(test::Character(ch));
    }
}

void InvokeCommand(BufferView& view, std::string_view name) {
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 20});
    view.OnEvent(test::Alt('x'));
    Type(view, std::string(name));
    view.OnEvent(test::Return());
}

std::string Content(const ned::text::Buffer& buffer) {
    return buffer.Content().Substring(0, buffer.Content().ByteLength());
}

} // namespace

TEST_CASE("fix-case-violation-at-point pre-fills the suggested name and Enter alone applies it",
          "[BufferView][CaseFix]") {
    const FormatRulesGuard guard;
    SetCaseConvention("parameter", CaseConvention::SnakeCase);

    const std::string source = "void f(int BadArg) {\n    int x = BadArg;\n}\n";
    Fixture           fixture(source);
    BufferView        view = fixture.View();

    // Land point on the parameter's own definition occurrence.
    fixture.buffer.SetPoint(source.find("BadArg"));

    InvokeCommand(view, "fix-case-violation-at-point");
    INFO("status: " << fixture.statusMessage);
    // The prompt's own label+text echo -- proof the suggestion is what got
    // pre-filled, not the violating name itself.
    REQUIRE(fixture.statusMessage.find("bad_arg") != std::string::npos);
    REQUIRE(fixture.statusMessage.find("BadArg") == std::string::npos);

    view.OnEvent(test::Return()); // accept the pre-filled suggestion as-is

    const std::string result = Content(fixture.buffer);
    REQUIRE(result.find("bad_arg") != std::string::npos);
    REQUIRE(result.find("BadArg") == std::string::npos);
    INFO("status: " << fixture.statusMessage);
    REQUIRE(fixture.statusMessage.find("bad_arg") != std::string::npos);
}

TEST_CASE("fix-case-violation-at-point reports nothing to fix when point isn't on a violation",
          "[BufferView][CaseFix]") {
    const FormatRulesGuard guard;
    SetCaseConvention("parameter", CaseConvention::SnakeCase);

    const std::string source = "void f(int good_arg) {\n}\n";
    Fixture           fixture(source);
    BufferView        view = fixture.View();
    fixture.buffer.SetPoint(source.find("good_arg"));

    InvokeCommand(view, "fix-case-violation-at-point");
    REQUIRE(fixture.statusMessage == "No case-convention violation at point.");
    REQUIRE(Content(fixture.buffer) == source); // untouched
}

