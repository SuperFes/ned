//
// rename-review follow-up: rename-symbol's edits routed through the editable
// review multibuffer instead of applied blind. The classification and layout
// are unit-tested purely in RenameReviewTest.cpp; what this file pins is the
// wiring -- that the review is what comes up, that the source buffer is
// untouched until a commit, that a comment occurrence the rename never asked
// for is listed and excluded until M-a includes it, and that turning the
// setting off restores the immediate rewrite exactly.
//

#include <catch2/catch_test_macros.hpp>

#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "Editor/Commands.h"
#include "Editor/Dispatcher.h"
#include "Editor/Keymap.h"
#include "Editor/Mode.h"
#include "Editor/ModeOverrides.h"
#include "Editor/Multibuffer.h"
#include "Editor/Project/Root.h"
#include "Editor/PromptHistory.h"
#include "Editor/Register.h"
#include "Editor/RenameReviewSettings.h"
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

struct RenameReviewGuard {
    explicit RenameReviewGuard(bool enabled) : previous_(ned::editor::RenameThroughReview()) {
        ned::editor::SetRenameThroughReview(enabled);
    }
    ~RenameReviewGuard() {
        ned::editor::SetRenameThroughReview(previous_);
    }
    bool previous_;
};

// A real file on disk, in its own project root, so the review has a path to
// commit back into and ModeForPath has an extension to classify by.
struct Fixture {
    RegistryResetGuard    registryResetGuard;
    ProjectRootResetGuard rootGuard;
    // Unique per process and per fixture: ctest runs each TEST_CASE in its
    // own process, in parallel, and a shared temp directory torn down in the
    // constructor would delete a sibling test's files out from under it.
    std::filesystem::path dir = std::filesystem::temp_directory_path() /
                                ("ned_rename_review_test_" + std::to_string(::getpid()) + "_" +
                                 std::to_string(NextFixtureId()));

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

    std::string                          statusMessage;
    std::filesystem::path                path;
    Buffer*                              source = nullptr;
    std::optional<ned::ui::ActiveBuffer> activeBuffer;

    Fixture(const std::string& fileName, const std::string& text) {
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        path = dir / fileName;
        std::ofstream(path) << text;

        ned::editor::SetProjectRoot(dir);
        mode   = ned::editor::ModeForPath(path);
        source = &bufferList.OpenOrCreateFile(path);
        activeBuffer.emplace(*source);
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

// C-c C-M-r, then replace the prefilled name with a new one.
void RunRename(BufferView& view, std::size_t oldNameLength, const std::string& newName) {
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 20});
    view.OnEvent(test::Ctrl('c'));
    view.OnEvent(test::CtrlAlt('r'));
    for (std::size_t i = 0; i < oldNameLength; ++i) {
        view.OnEvent(test::Backspace());
    }
    Type(view, newName);
    view.OnEvent(test::Return());
}

void CommitReview(BufferView& view) {
    view.OnEvent(test::Ctrl('c'));
    view.OnEvent(test::Ctrl('c'));
    view.OnEvent(test::Character("b"));
}

} // namespace

TEST_CASE("rename-symbol opens a review instead of rewriting the buffer", "[BufferView][RenameReview]") {
    const RenameReviewGuard review(true);
    const std::string       text = "void f(int size) {\n"
                                   "    // size is in bytes\n"
                                   "    int doubled = size + size;\n"
                                   "}\n";
    Fixture                 fixture("sample.c", text);
    BufferView              view = fixture.View();
    fixture.source->SetPoint(text.find("int size)") + 4);

    RunRename(view, 4, "width");

    // Nothing has been written into the source buffer.
    REQUIRE(Content(*fixture.source) == text);

    Buffer* const reviewBuffer = fixture.bufferList.Find("*rename*");
    REQUIRE(reviewBuffer != nullptr);
    REQUIRE(&fixture.activeBuffer->Get() == reviewBuffer);

    const std::string composite = Content(*reviewBuffer);
    // Both references carry the proposed name...
    CHECK(composite.find("void f(int width) {") != std::string::npos);
    CHECK(composite.find("int doubled = width + width;") != std::string::npos);
    // ...while the comment occurrence is listed, tagged, and left alone.
    CHECK(composite.find("[comment]") != std::string::npos);
    CHECK(composite.find("// size is in bytes") != std::string::npos);
    CHECK(composite.find("// width is in bytes") == std::string::npos);

    CHECK(fixture.statusMessage.find("2 references") != std::string::npos);
    CHECK(fixture.statusMessage.find("1 comment/string occurrence excluded") != std::string::npos);
}

TEST_CASE("committing a rename review writes only the included excerpts", "[BufferView][RenameReview]") {
    const RenameReviewGuard review(true);
    const std::string       text = "void f(int size) {\n"
                                   "    // size is in bytes\n"
                                   "    int doubled = size + size;\n"
                                   "}\n";
    Fixture                 fixture("sample.c", text);
    BufferView              view = fixture.View();
    fixture.source->SetPoint(text.find("int size)") + 4);

    RunRename(view, 4, "width");
    CommitReview(view);

    REQUIRE(Content(*fixture.source) == "void f(int width) {\n"
                                        "    // size is in bytes\n"
                                        "    int doubled = width + width;\n"
                                        "}\n");
}

TEST_CASE("M-a includes an excluded comment occurrence", "[BufferView][RenameReview]") {
    const RenameReviewGuard review(true);
    const std::string       text = "void f(int size) {\n"
                                   "    // size is in bytes\n"
                                   "    int doubled = size + size;\n"
                                   "}\n";
    Fixture                 fixture("sample.c", text);
    BufferView              view = fixture.View();
    fixture.source->SetPoint(text.find("int size)") + 4);

    RunRename(view, 4, "width");

    Buffer* const reviewBuffer = fixture.bufferList.Find("*rename*");
    REQUIRE(reviewBuffer != nullptr);
    // The comment row sorts last, behind both reference rows.
    const std::vector<Buffer::ExcerptRange>& ranges = reviewBuffer->ExcerptRanges();
    REQUIRE(ranges.size() == 3);
    reviewBuffer->SetPoint(ranges.back().start);

    view.OnEvent(test::Alt('a'));
    CHECK(Content(*reviewBuffer).find("// width is in bytes") != std::string::npos);

    // A second M-a has nothing left to include and says so rather than
    // cycling back to the original.
    view.OnEvent(test::Alt('a'));
    CHECK(fixture.statusMessage.find("already included") != std::string::npos);
    CHECK(Content(*reviewBuffer).find("// width is in bytes") != std::string::npos);

    CommitReview(view);
    CHECK(Content(*fixture.source) == "void f(int width) {\n"
                                      "    // width is in bytes\n"
                                      "    int doubled = width + width;\n"
                                      "}\n");
}

TEST_CASE("M-r excludes a reference the rename proposed", "[BufferView][RenameReview]") {
    const RenameReviewGuard review(true);
    const std::string       text = "void f(int size) {\n"
                                   "    int doubled = size + size;\n"
                                   "}\n";
    Fixture                 fixture("sample.c", text);
    BufferView              view = fixture.View();
    fixture.source->SetPoint(text.find("int size)") + 4);

    RunRename(view, 4, "width");

    Buffer* const reviewBuffer = fixture.bufferList.Find("*rename*");
    REQUIRE(reviewBuffer != nullptr);
    REQUIRE(reviewBuffer->ExcerptRanges().size() == 2);
    reviewBuffer->SetPoint(reviewBuffer->ExcerptRanges().back().start);

    view.OnEvent(test::Alt('r')); // multibuffer-revert-excerpt
    CommitReview(view);

    // Only the declaration was committed; reverting the second row put it
    // back to its original text, which is how an excerpt is excluded.
    CHECK(Content(*fixture.source) == "void f(int width) {\n"
                                      "    int doubled = size + size;\n"
                                      "}\n");
}

TEST_CASE("ned/set-rename-review false restores the immediate rewrite", "[BufferView][RenameReview]") {
    const RenameReviewGuard review(false);
    const std::string       text = "void f(int size) { return size; }\n";
    Fixture                 fixture("sample.c", text);
    BufferView              view = fixture.View();
    fixture.source->SetPoint(text.find("int size") + 4);

    RunRename(view, 4, "width");

    CHECK(Content(*fixture.source) == "void f(int width) { return width; }\n");
    CHECK(fixture.bufferList.Find("*rename*") == nullptr);
    CHECK(fixture.statusMessage.find("2 occurrences") != std::string::npos);
}
