//
// multibuffer-search-in-results / multibuffer-scoped-search follow-ups: the
// two halves of ROADMAP's "searching *within* a multibuffer" item, driven
// through BufferView the way a user does -- (b) narrowing a fresh search to
// the files a results buffer already names, and (a) isearch confining itself
// to excerpt bodies so it never stops on an excerpt's own header path.
//

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

#include "Editor/Commands.h"
#include "Editor/Dispatcher.h"
#include "Editor/Keymap.h"
#include "Editor/Mode.h"
#include "Editor/Multibuffer.h"
#include "Editor/MultibufferSearchSettings.h"
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

struct RegistryResetGuard {
    RegistryResetGuard() {
        ClearRegistryForTesting();
    }
    ~RegistryResetGuard() {
        ClearRegistryForTesting();
    }
};

struct ProjectRootResetGuard {
    std::filesystem::path saved = ned::editor::ProjectRoot();
    ~ProjectRootResetGuard() {
        ned::editor::SetProjectRoot(saved);
    }
};

struct ScopedSearchResetGuard {
    bool saved = ned::editor::MultibufferScopedSearch();
    ~ScopedSearchResetGuard() {
        ned::editor::SetMultibufferScopedSearch(saved);
    }
};

// Mirrors BufferViewProjectReplaceTest.cpp's own Fixture.
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

void Type(BufferView& view, const std::string& text) {
    for (const char c : text) {
        view.OnEvent(ned::ui::test::Character(std::string(1, c)));
    }
}

// C-c C-s, the real project-search binding.
void RunProjectSearch(BufferView& view, const std::string& pattern) {
    view.OnEvent(ned::ui::test::Ctrl('c'));
    view.OnEvent(ned::ui::test::Ctrl('s'));
    Type(view, pattern);
    view.OnEvent(ned::ui::test::Return());
}

// C-c s, the real search-in-results binding.
void RunSearchInResults(BufferView& view, const std::string& pattern) {
    view.OnEvent(ned::ui::test::Ctrl('c'));
    view.OnEvent(ned::ui::test::Character("s"));
    Type(view, pattern);
    view.OnEvent(ned::ui::test::Return());
}

} // namespace

TEST_CASE("C-c s narrows a fresh search to the files a flat results buffer already names",
          "[BufferView][SearchInResults]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_bufferview_search_in_results";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream(dir / "a.txt") << "needle\nshared\n";
        std::ofstream(dir / "b.txt") << "needle\nshared\n";
        std::ofstream(dir / "c.txt") << "shared\n"; // no needle -- never in the first result set
    }

    Fixture               fixture;
    ProjectRootResetGuard rootGuard;
    ned::editor::SetProjectRoot(dir);

    BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 20});

    RunProjectSearch(view, "needle");
    REQUIRE(fixture.activeBuffer.Get().Name().find("*search results*") == 0);

    RunSearchInResults(view, "shared");

    const std::string results = fixture.activeBuffer.Get().Text();
    REQUIRE(results.find((dir / "a.txt").string() + ":2: shared") != std::string::npos);
    REQUIRE(results.find((dir / "b.txt").string() + ":2: shared") != std::string::npos);
    // The narrowing is the whole point: c.txt matches "shared" but wasn't in
    // the result set, so it isn't searched.
    REQUIRE(results.find("c.txt") == std::string::npos);
    REQUIRE(fixture.statusMessage.find("2 matches for \"shared\" in 2 result files") == 0);

    std::filesystem::remove_all(dir);
}

TEST_CASE("C-c s narrows to a multibuffer's own excerpt sources", "[BufferView][SearchInResults]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_bufferview_search_in_multibuffer";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream(dir / "a.txt") << "alpha\n";
        std::ofstream(dir / "b.txt") << "alpha\n";
    }

    Fixture               fixture;
    ProjectRootResetGuard rootGuard;
    ned::editor::SetProjectRoot(dir);

    Buffer& composite =
        BuildMultibuffer(fixture.bufferList, "*references: alpha*",
                         {ExcerptSource{dir / "a.txt", 1, 1, (dir / "a.txt").string() + ":1", "alpha\n"}});
    fixture.activeBuffer.Set(composite);

    BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 20});

    RunSearchInResults(view, "alpha");

    const std::string results = fixture.activeBuffer.Get().Text();
    REQUIRE(fixture.activeBuffer.Get().Name().find("*search results*") == 0);
    REQUIRE(results.find((dir / "a.txt").string() + ":1: alpha") != std::string::npos);
    REQUIRE(results.find("b.txt") == std::string::npos); // not an excerpt source

    std::filesystem::remove_all(dir);
}

TEST_CASE("C-c s in an ordinary buffer says so instead of opening a prompt", "[BufferView][SearchInResults]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("just some text\n");

    BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 20});

    view.OnEvent(ned::ui::test::Ctrl('c'));
    view.OnEvent(ned::ui::test::Character("s"));

    REQUIRE(fixture.statusMessage == "No results in this buffer to search within.");

    // No prompt was opened, so ordinary editing resumes immediately.
    Type(view, "x");
    REQUIRE(fixture.buffer.Text() == "just some text\nx");
}

TEST_CASE("isearch in a multibuffer skips excerpt headers", "[BufferView][SearchInResults]") {
    Fixture                fixture;
    ScopedSearchResetGuard scopedGuard;
    ned::editor::SetMultibufferScopedSearch(true);

    Buffer& composite = BuildMultibuffer(fixture.bufferList, "*references: needle*",
                                         {ExcerptSource{"/repo/needle.txt", 1, 1, "/repo/needle.txt:1", "body needle\n"}});
    fixture.activeBuffer.Set(composite);
    composite.SetPoint(0);

    BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 20});

    view.OnEvent(ned::ui::test::Ctrl('s'));
    Type(view, "needle");

    // The header line names needle.txt and comes first; the match lands in
    // the body regardless.
    const std::size_t bodyNeedle = composite.Text().find("body needle") + std::string("body ").size();
    REQUIRE(composite.Point() == bodyNeedle + std::string("needle").size());
    REQUIRE(fixture.statusMessage.find("I-search (excerpts): needle") == 0);
}

TEST_CASE("Turning multibuffer scoped search off restores the whole-composite isearch",
          "[BufferView][SearchInResults]") {
    Fixture                fixture;
    ScopedSearchResetGuard scopedGuard;
    ned::editor::SetMultibufferScopedSearch(false);

    Buffer& composite = BuildMultibuffer(fixture.bufferList, "*references: needle*",
                                         {ExcerptSource{"/repo/needle.txt", 1, 1, "/repo/needle.txt:1", "body needle\n"}});
    fixture.activeBuffer.Set(composite);
    composite.SetPoint(0);

    BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 20});

    view.OnEvent(ned::ui::test::Ctrl('s'));
    Type(view, "needle");

    // Now the header's own "needle.txt" is the first match, exactly as it
    // was before this feature existed.
    const std::size_t headerNeedle = composite.Text().find("needle");
    REQUIRE(composite.Point() == headerNeedle + std::string("needle").size());
    REQUIRE(fixture.statusMessage.find("I-search: needle") == 0);
}
