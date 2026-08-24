#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

#include "Editor/Commands.h"
#include "Editor/Dispatcher.h"
#include "Editor/Keymap.h"
#include "Editor/Mode.h"
#include "Editor/Multibuffer.h"
#include "Editor/ProjectRoot.h"
#include "Editor/PromptHistory.h"
#include "Editor/Register.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "Text/KillRing.h"
#include "UI/ActiveBuffer.h"
#include "UI/BufferView.h"
#include "UI/Theme.h"

using ned::editor::multibuffer::ClearRegistryForTesting;
using ned::editor::multibuffer::MultibufferIndexFor;
using ned::text::Buffer;
using ned::ui::BufferView;

namespace {

// Mirrors MultibufferTest.cpp's own RegistryResetGuard -- without this, a
// Buffer destroyed at the end of one TEST_CASE can leave a stale registry
// entry a later TEST_CASE's freshly allocated Buffer spuriously "inherits"
// if the allocator reuses the same address.
struct RegistryResetGuard {
    RegistryResetGuard() {
        ClearRegistryForTesting();
    }
    ~RegistryResetGuard() {
        ClearRegistryForTesting();
    }
};

// Mirrors BufferViewDiagnosticsBufferTest.cpp's own Fixture exactly.
struct Fixture {
    RegistryResetGuard         registryResetGuard;
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

// find-all-references follow-up: ProjectRoot is process-wide state -- every
// test that sets one must restore it afterward, guaranteed via RAII (a
// failed REQUIRE partway through would skip a manual reset). Mirrors
// BufferViewDiffGutterTest.cpp's own inline save/restore.
struct ProjectRootGuard {
    std::filesystem::path previous = ned::editor::ProjectRoot();
    ~ProjectRootGuard() {
        ned::editor::SetProjectRoot(previous);
    }
};

} // namespace

TEST_CASE("RequestProjectFindReferences finds every whole-word match across the project, in one *references* multibuffer",
          "[BufferView][ProjectFindReferences]") {
    const ProjectRootGuard rootGuard;

    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_find_references_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream(dir / "a.cpp") << "int widget = 1;\nint other = widget + 1;\n";
    }
    {
        std::ofstream(dir / "b.cpp") << "void UseWidget(int widget) {}\n"; // "UseWidget" must NOT match -- not a whole word
    }
    ned::editor::SetProjectRoot(dir);

    Fixture fixture;
    fixture.buffer.InsertAtPoint("int widget = 1;\n");
    fixture.buffer.SetPoint(5); // inside "widget"

    BufferView view = fixture.View();
    view.RequestProjectFindReferencesForTesting();

    Buffer* results = fixture.bufferList.Find("*references: widget*");
    REQUIRE(results != nullptr);
    REQUIRE(results->ReadOnly());

    auto* index = MultibufferIndexFor(*results);
    REQUIRE(index != nullptr);
    // a.cpp has two whole-word occurrences of "widget" (the declaration and
    // the later use); b.cpp's "UseWidget"/parameter named "widget" -- only
    // the parameter is a whole word, "UseWidget" itself must not match.
    REQUIRE(index->Spans().size() == 3);

    REQUIRE(fixture.statusMessage.find("3 references to \"widget\"") == 0);

    std::filesystem::remove_all(dir);
}

TEST_CASE("RequestProjectFindReferences reports no identifier at point without building a buffer", "[BufferView][ProjectFindReferences]") {
    const ProjectRootGuard rootGuard;

    Fixture fixture;
    fixture.buffer.InsertAtPoint("   ");
    fixture.buffer.SetPoint(1); // sits on whitespace, not a word

    BufferView view = fixture.View();
    view.RequestProjectFindReferencesForTesting();

    REQUIRE(fixture.statusMessage == "No identifier at point.");
}
