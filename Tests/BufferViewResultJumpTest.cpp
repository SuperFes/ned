#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

#include "Editor/Commands.h"
#include "Editor/Dispatcher.h"
#include "Editor/Keymap.h"
#include "Editor/Mode.h"
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

using ned::editor::multibuffer::BuildMultibuffer;
using ned::editor::multibuffer::ClearRegistryForTesting;
using ned::editor::multibuffer::ExcerptSource;
using ned::text::Buffer;
using ned::ui::BufferView;

namespace {

// Mirrors MultibufferTest.cpp's own RegistryResetGuard -- see its doc
// comment for why the raw-Buffer*-keyed registry needs one.
struct RegistryResetGuard {
    RegistryResetGuard() {
        ClearRegistryForTesting();
    }
    ~RegistryResetGuard() {
        ClearRegistryForTesting();
    }
};

// ProjectRoot() is process-wide state -- restore whatever the rest of the
// suite was running with, same convention every other settings guard here
// follows.
struct ProjectRootResetGuard {
    std::filesystem::path saved = ned::editor::ProjectRoot();
    ~ProjectRootResetGuard() {
        ned::editor::SetProjectRoot(saved);
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

// vcs-visit-result (C-c v v), the keyboard route every results buffer's
// jump-to-source goes through -- BufferViewBlameGutterTest.cpp's own
// precedent for driving VisitResultUnderPoint from a test.
void VisitUnderPoint(BufferView& view) {
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 10});
    view.OnEvent(ned::ui::test::Ctrl('c'));
    view.OnEvent(ned::ui::test::Character("v"));
    view.OnEvent(ned::ui::test::Character("v"));
}

// The source buffer every excerpt test below points at: line 2 is
// "    return bogus;", so a column inside "bogus" is unambiguously not the
// line start a line-granularity jump would land on.
constexpr const char* kSourceText = "int main() {\n    return bogus;\n}\n";
constexpr std::size_t kLine2Start = 13;
constexpr const char* kLine2Body  = "    return bogus;\n";

} // namespace

// Multibuffer-gaps follow-up: byte-exact jump-to-source.

TEST_CASE("Visiting an editable excerpt preserves the column point sat at inside its body",
          "[BufferView][Multibuffer]") {
    Fixture fixture;

    Buffer& source = fixture.bufferList.CreateBuffer("a.cpp");
    source.SetPath("/repo/a.cpp");
    source.InsertAtPoint(kSourceText);

    Buffer& results = BuildMultibuffer(fixture.bufferList, "*references: bogus*",
                                       {ExcerptSource{"/repo/a.cpp", 2, 2, "a.cpp:2", kLine2Body, {}, /*editable=*/true}});
    REQUIRE(results.ExcerptRanges().size() == 1);

    // Point on the 'b' of "bogus" -- 11 bytes into line 2's own text.
    const std::size_t bodyStart = results.Text().find("    return bogus;");
    results.SetPoint(bodyStart + 11);
    fixture.activeBuffer.Set(results);

    BufferView view = fixture.View();
    VisitUnderPoint(view);

    REQUIRE(fixture.activeBuffer.Get().Name() == "a.cpp");
    REQUIRE(fixture.activeBuffer.Get().Point() == kLine2Start + 11);
}

TEST_CASE("Visiting a non-editable excerpt still lands on the excerpt's start line", "[BufferView][Multibuffer]") {
    Fixture fixture;

    Buffer& source = fixture.bufferList.CreateBuffer("a.cpp");
    source.SetPath("/repo/a.cpp");
    source.InsertAtPoint(kSourceText);

    // The agenda/clock-report/VCS-diff shape: a synthesized body with no
    // byte-for-byte relationship to the source, so nothing to map a column
    // through.
    Buffer& results = BuildMultibuffer(fixture.bufferList, "*agenda*",
                                       {ExcerptSource{"/repo/a.cpp", 2, 2, "a.cpp:2", "TODO write this up\n"}});
    REQUIRE(results.ExcerptRanges().empty());

    results.SetPoint(results.Text().find("TODO write this up") + 7);
    fixture.activeBuffer.Set(results);

    BufferView view = fixture.View();
    VisitUnderPoint(view);

    REQUIRE(fixture.activeBuffer.Get().Name() == "a.cpp");
    REQUIRE(fixture.activeBuffer.Get().Point() == kLine2Start);
}

TEST_CASE("An excerpt edited since the multibuffer was built falls back to a line-granularity jump",
          "[BufferView][Multibuffer]") {
    Fixture fixture;

    Buffer& source = fixture.bufferList.CreateBuffer("a.cpp");
    source.SetPath("/repo/a.cpp");
    source.InsertAtPoint(kSourceText);

    Buffer& results = BuildMultibuffer(fixture.bufferList, "*references: bogus*",
                                       {ExcerptSource{"/repo/a.cpp", 2, 2, "a.cpp:2", kLine2Body, {}, /*editable=*/true}});

    // A wgrep-style uncommitted edit inside the body: every byte typed
    // shifts the composite but nothing in the source, so the offset
    // arithmetic no longer holds.
    const std::size_t bodyStart = results.Text().find("    return bogus;");
    results.SetPoint(bodyStart + 4);
    results.InsertAtPoint("XY");
    results.SetPoint(bodyStart + 13);
    fixture.activeBuffer.Set(results);

    BufferView view = fixture.View();
    VisitUnderPoint(view);

    REQUIRE(fixture.activeBuffer.Get().Name() == "a.cpp");
    REQUIRE(fixture.activeBuffer.Get().Point() == kLine2Start);
}

TEST_CASE("An excerpt whose live source no longer matches the snapshot falls back to a line-granularity jump",
          "[BufferView][Multibuffer]") {
    Fixture fixture;

    Buffer& source = fixture.bufferList.CreateBuffer("a.cpp");
    source.SetPath("/repo/a.cpp");
    source.InsertAtPoint(kSourceText);

    Buffer& results = BuildMultibuffer(fixture.bufferList, "*references: bogus*",
                                       {ExcerptSource{"/repo/a.cpp", 2, 2, "a.cpp:2", kLine2Body, {}, /*editable=*/true}});

    // The source itself moved on since the excerpt was resolved -- the
    // stored byte range now covers different text entirely.
    source.SetPoint(0);
    source.InsertAtPoint("#include <cstdio>\n");

    results.SetPoint(results.Text().find("    return bogus;") + 11);
    fixture.activeBuffer.Set(results);

    BufferView view = fixture.View();
    VisitUnderPoint(view);

    REQUIRE(fixture.activeBuffer.Get().Name() == "a.cpp");
    // Line 2 of the *current* source, the same answer the pre-byte-exact
    // behavior gave.
    REQUIRE(fixture.activeBuffer.Get().Content().ByteOffsetToLine(fixture.activeBuffer.Get().Point()) == 1);
}

// Multibuffer-gaps follow-up: a results line whose path resolves to nothing.

TEST_CASE("A results line naming a path that doesn't exist reports a miss instead of creating an empty buffer",
          "[BufferView][Multibuffer]") {
    Fixture fixture;

    Buffer& results = fixture.bufferList.CreateBuffer("*search results*");
    results.InsertAtPoint("/nonexistent-ned-test-dir/missing.txt:3: boom\n");
    results.SetPoint(0);
    results.SetReadOnly(true);
    fixture.activeBuffer.Set(results);

    const std::size_t bufferCountBefore = fixture.bufferList.Buffers().size();

    BufferView view = fixture.View();
    VisitUnderPoint(view);

    REQUIRE(fixture.statusMessage == "No such file: /nonexistent-ned-test-dir/missing.txt");
    REQUIRE(fixture.bufferList.Buffers().size() == bufferCountBefore); // nothing created
    REQUIRE(fixture.activeBuffer.Get().Name() == "*search results*");  // and nothing switched to
}

TEST_CASE("A relative results-line path resolves against the project root, not the process's cwd",
          "[BufferView][Multibuffer]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_result_jump_relative_path";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "sub");
    {
        std::ofstream(dir / "sub" / "file.txt") << "one\ntwo\nthree\n";
    }

    Fixture               fixture;
    ProjectRootResetGuard rootGuard;
    ned::editor::SetProjectRoot(dir);

    Buffer& results = fixture.bufferList.CreateBuffer("*search results*");
    results.InsertAtPoint("sub/file.txt:2: two\n");
    results.SetPoint(0);
    results.SetReadOnly(true);
    fixture.activeBuffer.Set(results);

    BufferView view = fixture.View();
    VisitUnderPoint(view);

    REQUIRE(fixture.activeBuffer.Get().Name() == "file.txt");
    REQUIRE(fixture.activeBuffer.Get().Content().ByteOffsetToLine(fixture.activeBuffer.Get().Point()) == 1);

    std::filesystem::remove_all(dir);
}

TEST_CASE("Undoing an edit inside an excerpt restores the byte-exact jump", "[BufferView][Multibuffer]") {
    Fixture fixture;

    Buffer& source = fixture.bufferList.CreateBuffer("a.cpp");
    source.SetPath("/repo/a.cpp");
    source.InsertAtPoint(kSourceText);

    Buffer& results = BuildMultibuffer(fixture.bufferList, "*references: bogus*",
                                       {ExcerptSource{"/repo/a.cpp", 2, 2, "a.cpp:2", kLine2Body, {}, /*editable=*/true}});

    const std::size_t bodyStart = results.Text().find("    return bogus;");
    results.SetPoint(bodyStart + 17); // end of the body line, before its newline
    results.InsertAtPoint("\n");
    results.Undo();

    REQUIRE(results.Content().Substring(results.ExcerptRanges()[0].start,
                                        results.ExcerptRanges()[0].end - results.ExcerptRanges()[0].start) ==
            results.ExcerptRanges()[0].originalText);

    results.SetPoint(bodyStart + 17);
    fixture.activeBuffer.Set(results);

    BufferView view = fixture.View();
    VisitUnderPoint(view);

    REQUIRE(fixture.activeBuffer.Get().Name() == "a.cpp");
    REQUIRE(fixture.activeBuffer.Get().Point() == kLine2Start + 17);
}
