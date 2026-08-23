#include <catch2/catch_test_macros.hpp>

#include <string>

#include "Editor/Commands.h"
#include "Editor/Dispatcher.h"
#include "Editor/Keymap.h"
#include "Editor/Mode.h"
#include "Editor/Multibuffer.h"
#include "Editor/PromptHistory.h"
#include "Editor/Register.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "Text/KillRing.h"
#include "UI/ActiveBuffer.h"
#include "UI/BufferView.h"
#include "UI/Theme.h"

using ned::editor::multibuffer::MultibufferIndexFor;
using ned::text::Buffer;
using ned::ui::BufferView;

namespace {

// Mirrors BufferViewDiffGutterTest.cpp's own Fixture exactly.
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

} // namespace

TEST_CASE("RequestDiagnosticsBuffer stitches every open buffer's Code diagnostics into one *diagnostics* buffer",
          "[BufferView][Diagnostics]") {
    Fixture fixture;

    Buffer& a = fixture.bufferList.CreateBuffer("a.cpp");
    a.SetPath("/repo/a.cpp");
    a.InsertAtPoint("int main() {\n    return bogus;\n}\n");
    const std::size_t bogusStart = a.Text().find("bogus");
    a.SetDiagnostics({
        Buffer::Diagnostic{.startByte = bogusStart,
                           .endByte   = bogusStart + 5,
                           .severity  = Buffer::Diagnostic::Severity::Error,
                           .message   = "use of undeclared identifier 'bogus'"},
    });

    Buffer& b = fixture.bufferList.CreateBuffer("b.cpp");
    b.SetPath("/repo/b.cpp");
    b.InsertAtPoint("int x;\n");
    b.SetDiagnostics({
        Buffer::Diagnostic{
            .startByte = 4, .endByte = 5, .severity = Buffer::Diagnostic::Severity::Warning, .message = "unused variable 'x'"},
    });

    BufferView view = fixture.View();
    view.RequestDiagnosticsBufferForTesting();

    Buffer* results = fixture.bufferList.Find("*diagnostics*");
    REQUIRE(results != nullptr);
    REQUIRE(results->ReadOnly());

    auto* index = MultibufferIndexFor(*results);
    REQUIRE(index != nullptr);
    REQUIRE(index->Spans().size() == 2);

    // Grouped per file, path-sorted -- a.cpp's own entry precedes b.cpp's.
    REQUIRE(results->Text().find("a.cpp:2") < results->Text().find("b.cpp:1"));

    // The composite buffer carries its own real Diagnostic entries rather
    // than a bespoke LineTint -- the whole point being that the ordinary
    // gutter/underline/severity-color pipeline lights up unmodified.
    REQUIRE(results->Diagnostics().size() == 2);
    const Buffer::Diagnostic& first = results->Diagnostics()[0];
    REQUIRE(first.severity == Buffer::Diagnostic::Severity::Error);
    REQUIRE(first.message == "use of undeclared identifier 'bogus'");

    // startByte/endByte were translated to land on the real "bogus" token
    // inside the composite buffer's own copy of that source line.
    const std::string underlined = results->Content().Substring(first.startByte, first.endByte - first.startByte);
    REQUIRE(underlined == "bogus");

    const Buffer::Diagnostic& second = results->Diagnostics()[1];
    const std::string         underlinedX = results->Content().Substring(second.startByte, second.endByte - second.startByte);
    REQUIRE(underlinedX == "x");
}

TEST_CASE("RequestDiagnosticsBuffer ignores prose-origin diagnostics and buffers with none",
          "[BufferView][Diagnostics]") {
    Fixture fixture;

    Buffer& clean = fixture.bufferList.CreateBuffer("clean.cpp");
    clean.SetPath("/repo/clean.cpp");
    clean.InsertAtPoint("int x;\n");

    Buffer& prose = fixture.bufferList.CreateBuffer("notes.org");
    prose.SetPath("/repo/notes.org");
    prose.InsertAtPoint("teh quick fox\n");
    prose.SetDiagnostics({
        Buffer::Diagnostic{.startByte = 0,
                           .endByte   = 3,
                           .severity  = Buffer::Diagnostic::Severity::Hint,
                           .origin    = Buffer::Diagnostic::Origin::Prose,
                           .message   = "spelling"},
    });

    BufferView view = fixture.View();
    view.RequestDiagnosticsBufferForTesting();

    Buffer* results = fixture.bufferList.Find("*diagnostics*");
    REQUIRE(results != nullptr);
    REQUIRE(results->Diagnostics().empty());
    REQUIRE(fixture.statusMessage == "No diagnostics.");
}
