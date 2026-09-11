//
// file-rename-propagation follow-up: renaming a file rewrites the imports
// that named it, and the relative imports it wrote itself. The arithmetic
// and the planning are unit-tested purely in ImportFixupTest.cpp; what this
// file pins is the wiring -- that a rename opens the "*imports*" review, that
// committing it writes the new specifier into the real source buffer, and
// that turning the setting off leaves every import exactly as it was.
//

#include <catch2/catch_test_macros.hpp>

#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "Editor/Commands.h"
#include "Editor/Dispatcher.h"
#include "Editor/ImportFixupSettings.h"
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

struct ImportFixupGuard {
    explicit ImportFixupGuard(bool enabled) : previous_(ned::editor::ImportFixupEnabled()) {
        ned::editor::SetImportFixupEnabled(enabled);
    }
    ~ImportFixupGuard() {
        ned::editor::SetImportFixupEnabled(previous_);
    }
    bool previous_;
};

std::size_t NextFixtureId() {
    static std::size_t next = 0;
    return next++;
}

// A two-file project on disk: one file importing the other, both real, so
// the import resolves the way it would in a real tree.
struct Fixture {
    RegistryResetGuard    registryResetGuard;
    ProjectRootResetGuard rootGuard;
    std::filesystem::path dir = std::filesystem::temp_directory_path() /
                                ("ned_import_fixup_test_" + std::to_string(::getpid()) + "_" +
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
    Buffer*                              opened = nullptr;
    std::optional<ned::ui::ActiveBuffer> activeBuffer;

    Fixture() {
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir / "Editor");
        std::filesystem::create_directories(dir / "UI");
        // RenameProjectPath does not create the destination's parents.
        std::filesystem::create_directories(dir / "Text");
        std::filesystem::create_directories(dir / "UI" / "deep");
        std::ofstream(dir / "Editor" / "Widget.h") << "#pragma once\n";
        std::ofstream(dir / "UI" / "Pane.cpp") << "#include \"Editor/Widget.h\"\n\nint main() {}\n";

        ned::editor::SetProjectRoot(dir);
        mode   = ned::editor::ModeForPath(dir / "UI" / "Pane.cpp");
        opened = &bufferList.OpenOrCreateFile(dir / "UI" / "Pane.cpp");
        activeBuffer.emplace(*opened);
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

// C-c C-n: rename-file, source prompt then destination prompt.
void RunRenameFile(BufferView& view, const std::filesystem::path& from, const std::filesystem::path& to) {
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 20});
    view.OnEvent(test::Ctrl('c'));
    view.OnEvent(test::Ctrl('n'));
    Type(view, from.string());
    view.OnEvent(test::Return());
    Type(view, to.string());
    view.OnEvent(test::Return());
}

} // namespace

TEST_CASE("Renaming a file opens an import review naming the files that imported it", "[BufferView][ImportFixup]") {
    const ImportFixupGuard fixups(true);
    Fixture                fixture;
    BufferView             view = fixture.View();

    RunRenameFile(view, fixture.dir / "Editor" / "Widget.h", fixture.dir / "Text" / "Widget.h");

    INFO("status: " << fixture.statusMessage);
    Buffer* const review = fixture.bufferList.Find("*imports*");
    REQUIRE(review != nullptr);
    REQUIRE(&fixture.activeBuffer->Get() == review);

    const std::string composite = Content(*review);
    CHECK(composite.find("#include \"Text/Widget.h\"") != std::string::npos);
    CHECK(composite.find("UI/Pane.cpp") != std::string::npos);
    CHECK(fixture.statusMessage.find("1 import in 1 file") != std::string::npos);

    // Nothing is written until the review is committed.
    CHECK(Content(*fixture.opened) == "#include \"Editor/Widget.h\"\n\nint main() {}\n");
}

TEST_CASE("Committing the import review rewrites the real source buffer", "[BufferView][ImportFixup]") {
    const ImportFixupGuard fixups(true);
    Fixture                fixture;
    BufferView             view = fixture.View();

    RunRenameFile(view, fixture.dir / "Editor" / "Widget.h", fixture.dir / "Text" / "Widget.h");
    REQUIRE(fixture.bufferList.Find("*imports*") != nullptr);

    view.OnEvent(test::Ctrl('c'));
    view.OnEvent(test::Ctrl('c'));
    view.OnEvent(test::Character("b")); // into the open buffers

    CHECK(Content(*fixture.opened) == "#include \"Text/Widget.h\"\n\nint main() {}\n");
}

TEST_CASE("An excerpt reverted in the import review commits nothing", "[BufferView][ImportFixup]") {
    const ImportFixupGuard fixups(true);
    Fixture                fixture;
    BufferView             view = fixture.View();

    RunRenameFile(view, fixture.dir / "Editor" / "Widget.h", fixture.dir / "Text" / "Widget.h");
    Buffer* const review = fixture.bufferList.Find("*imports*");
    REQUIRE(review != nullptr);
    REQUIRE_FALSE(review->ExcerptRanges().empty());

    review->SetPoint(review->ExcerptRanges().front().start);
    view.OnEvent(test::Alt('r')); // exclude the excerpt under point
    view.OnEvent(test::Ctrl('c'));
    view.OnEvent(test::Ctrl('c'));
    view.OnEvent(test::Character("b"));

    CHECK(Content(*fixture.opened) == "#include \"Editor/Widget.h\"\n\nint main() {}\n");
}

TEST_CASE("With import fixup off, a rename touches no import at all", "[BufferView][ImportFixup]") {
    const ImportFixupGuard fixups(false);
    Fixture                fixture;
    BufferView             view = fixture.View();

    RunRenameFile(view, fixture.dir / "Editor" / "Widget.h", fixture.dir / "Text" / "Widget.h");

    CHECK(fixture.bufferList.Find("*imports*") == nullptr);
    CHECK(fixture.statusMessage.find("Renamed to") != std::string::npos);
    CHECK(Content(*fixture.opened) == "#include \"Editor/Widget.h\"\n\nint main() {}\n");
}

TEST_CASE("A moved file's own relative imports are rewritten too", "[BufferView][ImportFixup]") {
    const ImportFixupGuard fixups(true);
    Fixture                fixture;
    // Pane.cpp also includes a sibling, which has to become "../UI/Local.h"
    // once Pane.cpp itself moves one directory deeper.
    std::ofstream(fixture.dir / "UI" / "Local.h") << "#pragma once\n";
    {
        std::ofstream out(fixture.dir / "UI" / "Pane.cpp");
        out << "#include \"Local.h\"\n\nint main() {}\n";
    }
    fixture.opened->Revert();

    BufferView view = fixture.View();
    RunRenameFile(view, fixture.dir / "UI" / "Pane.cpp", fixture.dir / "UI" / "deep" / "Pane.cpp");

    Buffer* const review = fixture.bufferList.Find("*imports*");
    REQUIRE(review != nullptr);
    CHECK(Content(*review).find("#include \"../Local.h\"") != std::string::npos);
}
