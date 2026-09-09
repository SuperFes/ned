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
#include "Editor/Project/Undo.h"
#include "Editor/PromptHistory.h"
#include "Editor/Register.h"
#include "TestEvents.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "Text/KillRing.h"
#include "UI/ActiveBuffer.h"
#include "UI/BufferView.h"
#include "UI/Theme.h"

using ned::editor::multibuffer::ClearRegistryForTesting;
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

// Mirrors BufferViewDiagnosticsBufferTest.cpp's own Fixture.
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

// C-c C-r, the real project-replace binding, then the pattern and the
// replacement -- the whole interactive flow as a user drives it.
void RunProjectReplace(BufferView& view, const std::string& pattern, const std::string& replacement) {
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 20});
    view.OnEvent(ned::ui::test::Ctrl('c'));
    view.OnEvent(ned::ui::test::Ctrl('r'));
    Type(view, pattern);
    view.OnEvent(ned::ui::test::Return());
    Type(view, replacement);
    view.OnEvent(ned::ui::test::Return());
}

// C-c C-c asks where the reviewed text should land; 'b' is "into the open
// source buffers", the reviewable default.
void CommitReview(BufferView& view) {
    view.OnEvent(ned::ui::test::Ctrl('c'));
    view.OnEvent(ned::ui::test::Ctrl('c'));
    view.OnEvent(ned::ui::test::Character("b"));
}

void CommitReviewToDisk(BufferView& view) {
    view.OnEvent(ned::ui::test::Ctrl('c'));
    view.OnEvent(ned::ui::test::Ctrl('c'));
    view.OnEvent(ned::ui::test::Character("d"));
}

} // namespace

TEST_CASE("project-replace builds an editable review buffer and writes nothing until it's committed",
          "[BufferView][ProjectReplace]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_bufferview_project_replace_review";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream(dir / "a.txt") << "alpha needle omega\n";
    }

    Fixture               fixture;
    ProjectRootResetGuard rootGuard;
    ned::editor::SetProjectRoot(dir);

    BufferView view = fixture.View();
    RunProjectReplace(view, "needle", "thread");

    Buffer* review = fixture.bufferList.Find("*project replace*");
    REQUIRE(review != nullptr);
    REQUIRE(fixture.activeBuffer.Get().Name() == "*project replace*");

    // The excerpt body is already rewritten, so the review shows the result,
    // not the search hit.
    REQUIRE(review->Text().find("alpha thread omega") != std::string::npos);
    REQUIRE_FALSE(review->ReadOnly());
    REQUIRE(review->ExcerptRanges().size() == 1);

    // Nothing on disk has changed yet.
    std::ifstream     check(dir / "a.txt");
    const std::string onDisk((std::istreambuf_iterator<char>(check)), std::istreambuf_iterator<char>());
    REQUIRE(onDisk == "alpha needle omega\n");

    std::filesystem::remove_all(dir);
}

TEST_CASE("Committing the review writes into a live buffer, still without touching disk",
          "[BufferView][ProjectReplace]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_bufferview_project_replace_commit";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream(dir / "a.txt") << "alpha needle omega\n";
    }

    Fixture               fixture;
    ProjectRootResetGuard rootGuard;
    ned::editor::SetProjectRoot(dir);

    BufferView view = fixture.View();
    RunProjectReplace(view, "needle", "thread");
    CommitReview(view);

    // The source is now an open, modified buffer carrying the replacement...
    Buffer* source = fixture.bufferList.FindByPath(dir / "a.txt");
    REQUIRE(source != nullptr);
    REQUIRE(source->Text() == "alpha thread omega\n");
    REQUIRE(source->Modified());

    // ...and the file itself is untouched until the user saves.
    std::ifstream     check(dir / "a.txt");
    const std::string onDisk((std::istreambuf_iterator<char>(check)), std::istreambuf_iterator<char>());
    REQUIRE(onDisk == "alpha needle omega\n");

    std::filesystem::remove_all(dir);
}

TEST_CASE("An excerpt edited back to its original text is skipped by the commit", "[BufferView][ProjectReplace]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_bufferview_project_replace_skip";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream(dir / "keep.txt") << "keep needle here\n";
    }
    {
        std::ofstream(dir / "change.txt") << "change needle here\n";
    }

    Fixture               fixture;
    ProjectRootResetGuard rootGuard;
    ned::editor::SetProjectRoot(dir);

    BufferView view = fixture.View();
    RunProjectReplace(view, "needle", "thread");

    Buffer* review = fixture.bufferList.Find("*project replace*");
    REQUIRE(review != nullptr);

    // Put keep.txt's excerpt back the way it was -- the "I don't want this
    // one" gesture, needing no special exclusion mechanism: an unchanged
    // excerpt is one CommitExcerptChanges leaves alone.
    const std::size_t keepBody = review->Text().find("keep thread here");
    REQUIRE(keepBody != std::string::npos);
    review->SetPoint(keepBody);
    review->DeleteRange(keepBody, std::string("keep thread here").size());
    review->InsertAt(keepBody, "keep needle here");

    CommitReview(view);

    Buffer* keep    = fixture.bufferList.FindByPath(dir / "keep.txt");
    Buffer* changed = fixture.bufferList.FindByPath(dir / "change.txt");
    REQUIRE(changed != nullptr);
    REQUIRE(changed->Text() == "change thread here\n");
    REQUIRE((keep == nullptr || keep->Text() == "keep needle here\n"));

    std::filesystem::remove_all(dir);
}

TEST_CASE("A replace spanning several files commits as one undoable project transaction",
          "[BufferView][ProjectReplace]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_bufferview_project_replace_undo";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream(dir / "a.txt") << "a needle\n";
    }
    {
        std::ofstream(dir / "b.txt") << "b needle\n";
    }

    Fixture                         fixture;
    ProjectRootResetGuard           rootGuard;
    ned::editor::ProjectUndoManager projectUndo;
    ned::editor::SetProjectRoot(dir);

    BufferView view = fixture.View();
    view.SetProjectUndo(&projectUndo);
    RunProjectReplace(view, "needle", "thread");
    CommitReview(view);

    Buffer* a = fixture.bufferList.FindByPath(dir / "a.txt");
    Buffer* b = fixture.bufferList.FindByPath(dir / "b.txt");
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    REQUIRE(a->Text() == "a thread\n");
    REQUIRE(b->Text() == "b thread\n");

    REQUIRE(projectUndo.CanUndo());
    const ned::editor::ProjectUndoOutcome outcome = projectUndo.Undo(fixture.bufferList);
    REQUIRE(outcome.appliedCount == 2);
    REQUIRE(a->Text() == "a needle\n"); // both files back out together
    REQUIRE(b->Text() == "b needle\n");

    std::filesystem::remove_all(dir);
}

TEST_CASE("project-replace previews an open buffer's unsaved edits, not its file", "[BufferView][ProjectReplace]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_bufferview_project_replace_live";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream(dir / "a.txt") << "nothing here\n";
    }

    Fixture               fixture;
    ProjectRootResetGuard rootGuard;
    ned::editor::SetProjectRoot(dir);

    Buffer& open = fixture.bufferList.OpenOrCreateFile(dir / "a.txt");
    open.SetPoint(open.Content().ByteLength());
    open.InsertAtPoint("typed needle, never saved\n");

    BufferView view = fixture.View();
    RunProjectReplace(view, "needle", "thread");

    Buffer* review = fixture.bufferList.Find("*project replace*");
    REQUIRE(review != nullptr);
    REQUIRE(review->Text().find("typed thread, never saved") != std::string::npos);

    std::filesystem::remove_all(dir);
}

// project-replace-review follow-up: the apply chooser and the review's own
// content-scoped quick keys.

TEST_CASE("The apply prompt offers buffers or disk, and cancels on anything else", "[BufferView][ProjectReplace]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_bufferview_project_replace_prompt";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream(dir / "a.txt") << "alpha needle omega\n";
    }

    Fixture               fixture;
    ProjectRootResetGuard rootGuard;
    ned::editor::SetProjectRoot(dir);

    BufferView view = fixture.View();
    RunProjectReplace(view, "needle", "thread");

    view.OnEvent(ned::ui::test::Ctrl('c'));
    view.OnEvent(ned::ui::test::Ctrl('c'));
    REQUIRE(fixture.statusMessage.find("(b) into open buffers") != std::string::npos);
    REQUIRE(fixture.statusMessage.find("(d) write files directly") != std::string::npos);

    view.OnEvent(ned::ui::test::Escape());
    REQUIRE(fixture.statusMessage == "Apply cancelled.");
    REQUIRE(fixture.bufferList.FindByPath(dir / "a.txt") == nullptr); // nothing applied anywhere

    std::filesystem::remove_all(dir);
}

TEST_CASE("Choosing 'd' writes the files directly and opens no buffer", "[BufferView][ProjectReplace]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_bufferview_project_replace_disk";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream(dir / "a.txt") << "alpha needle omega\n";
    }

    Fixture               fixture;
    ProjectRootResetGuard rootGuard;
    ned::editor::SetProjectRoot(dir);

    BufferView view = fixture.View();
    RunProjectReplace(view, "needle", "thread");
    CommitReviewToDisk(view);

    std::ifstream     check(dir / "a.txt");
    const std::string onDisk((std::istreambuf_iterator<char>(check)), std::istreambuf_iterator<char>());
    REQUIRE(onDisk == "alpha thread omega\n"); // sed-flavored: the file itself
    REQUIRE(fixture.bufferList.FindByPath(dir / "a.txt") == nullptr);
    REQUIRE(fixture.statusMessage.find("wrote 1 file") != std::string::npos);

    std::filesystem::remove_all(dir);
}

TEST_CASE("M-r reverts the excerpt under point, M-R its whole file", "[BufferView][ProjectReplace]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_bufferview_project_replace_revert";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream(dir / "a.txt") << "one needle\ntwo needle\n";
    }
    {
        std::ofstream(dir / "b.txt") << "three needle\n";
    }

    Fixture               fixture;
    ProjectRootResetGuard rootGuard;
    ned::editor::SetProjectRoot(dir);

    BufferView view = fixture.View();
    RunProjectReplace(view, "needle", "thread");

    ned::text::Buffer* review = fixture.bufferList.Find("*project replace*");
    REQUIRE(review != nullptr);

    // M-r on one of a.txt's two excerpts puts just that one back.
    review->SetPoint(review->Text().find("one thread"));
    view.OnEvent(ned::ui::test::Alt('r'));
    REQUIRE(review->Text().find("one needle") != std::string::npos);
    REQUIRE(review->Text().find("two thread") != std::string::npos);

    // M-R -- the shifted letter is its own codepoint chord, the same trick
    // C-c v H uses beside its lowercase twin.
    review->SetPoint(review->Text().find("two thread"));
    view.OnEvent(ned::ui::test::Alt('R'));
    REQUIRE(review->Text().find("two needle") != std::string::npos);
    REQUIRE(review->Text().find("three thread") != std::string::npos); // b.txt untouched

    std::filesystem::remove_all(dir);
}

TEST_CASE("M-c applies only the file under point, leaving the rest of the review pending",
          "[BufferView][ProjectReplace]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_bufferview_project_replace_perfile";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream(dir / "a.txt") << "a needle\n";
    }
    {
        std::ofstream(dir / "b.txt") << "b needle\n";
    }

    Fixture               fixture;
    ProjectRootResetGuard rootGuard;
    ned::editor::SetProjectRoot(dir);

    BufferView view = fixture.View();
    RunProjectReplace(view, "needle", "thread");

    ned::text::Buffer* review = fixture.bufferList.Find("*project replace*");
    REQUIRE(review != nullptr);
    review->SetPoint(review->Text().find("a thread"));

    view.OnEvent(ned::ui::test::Alt('c')); // same chooser, narrower scope
    REQUIRE(fixture.statusMessage.find("Apply this file") == 0);
    view.OnEvent(ned::ui::test::Character("b"));

    REQUIRE(fixture.bufferList.FindByPath(dir / "a.txt") != nullptr);
    REQUIRE(fixture.bufferList.FindByPath(dir / "b.txt") == nullptr); // still pending in the review

    std::filesystem::remove_all(dir);
}

TEST_CASE("Every occurrence on a matched line is replaced, not just the first",
          "[BufferView][ProjectReplace]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_bufferview_project_replace_multiocc";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream(dir / "a.txt") << "cat and cat and cat\n";
    }

    Fixture               fixture;
    ProjectRootResetGuard rootGuard;
    ned::editor::SetProjectRoot(dir);

    BufferView view = fixture.View();
    RunProjectReplace(view, "cat", "dog");

    REQUIRE(fixture.activeBuffer.Get().Text().find("dog and dog and dog") != std::string::npos);
    REQUIRE(fixture.statusMessage.find("3 replacements") == 0);

    std::filesystem::remove_all(dir);
}

TEST_CASE("A replace across multi-byte UTF-8 content leaves the surrounding codepoints intact",
          "[BufferView][ProjectReplace]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_bufferview_project_replace_utf8";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream(dir / "a.txt") << "héllo wörld cat ünïcode\n";
    }

    Fixture               fixture;
    ProjectRootResetGuard rootGuard;
    ned::editor::SetProjectRoot(dir);

    BufferView view = fixture.View();
    RunProjectReplace(view, "cat", "dog");
    CommitReviewToDisk(view);

    std::ifstream     check(dir / "a.txt");
    const std::string onDisk((std::istreambuf_iterator<char>(check)), std::istreambuf_iterator<char>());
    REQUIRE(onDisk == "héllo wörld dog ünïcode\n");

    std::filesystem::remove_all(dir);
}

TEST_CASE("Answering 'd' at the per-file prompt writes that one file, not its buffer",
          "[BufferView][ProjectReplace]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_bufferview_project_replace_perfile_disk";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream(dir / "a.txt") << "a needle\n";
    }
    {
        std::ofstream(dir / "b.txt") << "b needle\n";
    }

    Fixture               fixture;
    ProjectRootResetGuard rootGuard;
    ned::editor::SetProjectRoot(dir);

    BufferView view = fixture.View();
    RunProjectReplace(view, "needle", "thread");

    ned::text::Buffer* review = fixture.bufferList.Find("*project replace*");
    REQUIRE(review != nullptr);
    review->SetPoint(review->Text().find("a thread"));

    view.OnEvent(ned::ui::test::Alt('c'));
    view.OnEvent(ned::ui::test::Character("d"));

    const auto read = [](const std::filesystem::path& p) {
        std::ifstream f(p, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    };
    REQUIRE(read(dir / "a.txt") == "a thread\n");                  // written directly
    REQUIRE(read(dir / "b.txt") == "b needle\n");                  // still pending in the review
    REQUIRE(fixture.bufferList.FindByPath(dir / "a.txt") == nullptr); // and no buffer opened for it

    std::filesystem::remove_all(dir);
}
