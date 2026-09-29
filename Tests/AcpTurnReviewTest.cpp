//
// Editor/Acp/TurnFiles.h and TurnReview.h -- a turn's changes as hunks,
// kept or undone against the file as it reads now.
//

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "Editor/Acp/TurnFiles.h"
#include "Editor/Acp/TurnReview.h"
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
#include "UI/AcpPanel/TranscriptFormat.h"
#include "UI/ActiveBuffer.h"
#include "UI/BufferView.h"
#include "UI/Theme.h"

using ned::editor::acp::FileState;
using ned::editor::acp::HunkState;
using ned::editor::acp::ReviewHunk;
using ned::editor::acp::TurnFile;

namespace {

TurnFile Changed(std::string before, std::string after) {
    return TurnFile{.path   = "/p/f.txt",
                    .before = FileState{.exists = true, .text = std::move(before)},
                    .after  = FileState{.exists = true, .text = std::move(after)}};
}

FileState Text(std::string text) {
    return FileState{.exists = true, .text = std::move(text)};
}

std::string Numbered(int count) {
    std::string text;
    for (int i = 1; i <= count; ++i) {
        text += "line " + std::to_string(i) + "\n";
    }
    return text;
}

std::string ReadWhole(const std::filesystem::path& path) {
    std::ifstream      input(path, std::ios::binary);
    std::ostringstream content;
    content << input.rdbuf();
    return content.str();
}

void WriteWhole(const std::filesystem::path& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << text;
}

} // namespace

TEST_CASE("TurnHunks joins nearby changes and keeps unchanged context around each", "[AcpReview]") {
    std::string after = Numbered(30);
    after.replace(after.find("line 5\n"), 7, "FIVE\n");
    after.replace(after.find("line 8\n"), 7, "EIGHT\n");
    after.replace(after.find("line 25\n"), 8, "");
    const auto hunks = ned::editor::acp::TurnHunks(Changed(Numbered(30), after), 0);
    REQUIRE(hunks.size() == 2);
    REQUIRE(hunks[0].oldLines == std::vector<std::string>{"line 5\n", "line 6\n", "line 7\n", "line 8\n"});
    REQUIRE(hunks[0].newLines == std::vector<std::string>{"FIVE\n", "line 6\n", "line 7\n", "EIGHT\n"});
    REQUIRE(hunks[0].contextBefore == std::vector<std::string>{"line 2\n", "line 3\n", "line 4\n"});
    REQUIRE(hunks[1].oldLines == std::vector<std::string>{"line 25\n"});
    REQUIRE(hunks[1].newLines.empty());
    REQUIRE(hunks[1].contextAfter.front() == "line 26\n");
}

TEST_CASE("RevertHunk finds its change again after lines were added above it", "[AcpReview]") {
    const auto hunks = ned::editor::acp::TurnHunks(Changed("a\nb\nc\nd\n", "a\nb\nC\nd\n"), 0);
    REQUIRE(hunks.size() == 1);
    const FileState now = Text("new 1\nnew 2\na\nb\nC\nd\n");
    REQUIRE(ned::editor::acp::StateOf(now, hunks[0]) == HunkState::Applied);
    const auto reverted = ned::editor::acp::RevertHunk(now, hunks[0]);
    REQUIRE(reverted);
    REQUIRE(reverted->text == "new 1\nnew 2\na\nb\nc\nd\n");
    REQUIRE(ned::editor::acp::StateOf(*reverted, hunks[0]) == HunkState::Undone);
}

TEST_CASE("RevertHunk refuses a change that was edited again since", "[AcpReview]") {
    const auto      hunks = ned::editor::acp::TurnHunks(Changed("a\nb\nc\n", "a\nB\nc\n"), 0);
    const FileState now   = Text("a\nBee\nc\n");
    REQUIRE(ned::editor::acp::StateOf(now, hunks[0]) == HunkState::Changed);
    REQUIRE_FALSE(ned::editor::acp::RevertHunk(now, hunks[0]));
}

TEST_CASE("A created file is one hunk whose undo deletes it, and a deleted one comes back", "[AcpReview]") {
    const TurnFile created{.path = "/p/new.txt", .before = FileState{}, .after = Text("x\ny\n")};
    const auto     made = ned::editor::acp::TurnHunks(created, 0);
    REQUIRE(made.size() == 1);
    REQUIRE(made[0].created);
    REQUIRE_FALSE(ned::editor::acp::RevertHunk(Text("x\ny\nmine\n"), made[0]));
    REQUIRE(ned::editor::acp::RevertHunk(Text("x\ny\n"), made[0]) == FileState{});

    const TurnFile deleted{.path = "/p/old.txt", .before = Text("gone\n"), .after = FileState{}};
    const auto     removed = ned::editor::acp::TurnHunks(deleted, 0);
    REQUIRE(removed[0].deleted);
    REQUIRE(ned::editor::acp::RevertHunk(FileState{}, removed[0]) == Text("gone\n"));
}

TEST_CASE("ReviewExcerpts heads each hunk with its file, line, size and state", "[AcpReview]") {
    ned::text::BufferList       bufferList;
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned-acp-review-excerpts.txt";
    WriteWhole(path, "a\nB\nc\n");
    TurnFile file   = Changed("a\nb\nc\n", "a\nB\nc\n");
    file.path       = path;
    auto session    = ned::editor::acp::MakeReviewSession("fix b", path.parent_path(), {file});
    session.kept[0] = true;

    auto excerpts = ned::editor::acp::ReviewExcerpts(bufferList, session);
    REQUIRE(excerpts.size() == 1);
    REQUIRE(excerpts[0].headerText == "▸ ned-acp-review-excerpts.txt:2  +1 −1  ✓ kept");
    REQUIRE(excerpts[0].bodyText == "  a\n- b\n+ B\n  c\n");
    REQUIRE(excerpts[0].sourceStartLine == 2);

    WriteWhole(path, "a\nb\nc\n");
    excerpts = ned::editor::acp::ReviewExcerpts(bufferList, session);
    REQUIRE(excerpts[0].headerText.ends_with("↶ undone"));
    std::filesystem::remove(path);
}

TEST_CASE("UndoHunk writes the file and brings an unmodified open buffer along", "[AcpReview]") {
    ned::text::BufferList       bufferList;
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned-acp-review-undo.txt";
    WriteWhole(path, "a\nB\nc\n");
    ned::text::Buffer& buffer = bufferList.OpenOrCreateFile(path);
    TurnFile           file   = Changed("a\nb\nc\n", "a\nB\nc\n");
    file.path                 = path;
    auto session              = ned::editor::acp::MakeReviewSession("fix b", path.parent_path(), {file});

    REQUIRE(ned::editor::acp::UndoHunk(bufferList, session, 0) == "Undid a change in ned-acp-review-undo.txt.");
    REQUIRE(ReadWhole(path) == "a\nb\nc\n");
    REQUIRE(buffer.Text() == "a\nb\nc\n");
    REQUIRE_FALSE(buffer.Modified());
    REQUIRE(ned::editor::acp::UndoHunk(bufferList, session, 0) == "Already undone.");
    std::filesystem::remove(path);
}

TEST_CASE("UndoFile edits a buffer with unsaved changes in place and leaves it unsaved", "[AcpReview]") {
    ned::text::BufferList       bufferList;
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned-acp-review-unsaved.txt";
    const std::string           middle = "three\nfour\nfive\nsix\nseven\neight\nnine\n";
    WriteWhole(path, "one\nTWO\n" + middle + "TEN\n");
    ned::text::Buffer& buffer = bufferList.OpenOrCreateFile(path);
    buffer.SetPoint(0);
    buffer.InsertAtPoint("mine\n");
    TurnFile file = Changed("one\ntwo\n" + middle + "ten\n", "one\nTWO\n" + middle + "TEN\n");
    file.path     = path;
    auto session  = ned::editor::acp::MakeReviewSession("caps", path.parent_path(), {file});
    REQUIRE(session.hunks.size() == 2);

    REQUIRE(ned::editor::acp::UndoFile(bufferList, session, 1) == "Undid 2 changes in ned-acp-review-unsaved.txt.");
    REQUIRE(buffer.Text() == "mine\none\ntwo\n" + middle + "ten\n");
    REQUIRE(buffer.Modified());
    REQUIRE(ReadWhole(path) == "one\nTWO\n" + middle + "TEN\n");
    std::filesystem::remove(path);
}

TEST_CASE("ReadTurnFile prefers an open buffer's text and won't track a binary file", "[AcpReview]") {
    ned::text::BufferList       bufferList;
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned-acp-review-read.txt";
    WriteWhole(path, "disk\n");
    ned::text::Buffer& buffer = bufferList.OpenOrCreateFile(path);
    buffer.SetPoint(0);
    buffer.InsertAtPoint("live ");
    REQUIRE(ned::editor::acp::ReadTurnFile(bufferList, path)->text == "live disk\n");

    const std::filesystem::path binary = std::filesystem::temp_directory_path() / "ned-acp-review-read.bin";
    WriteWhole(binary, std::string("a\0b", 3));
    REQUIRE_FALSE(ned::editor::acp::ReadTurnFile(bufferList, binary));
    REQUIRE(ned::editor::acp::ReadTurnFile(bufferList, "/nonexistent/ned-acp-review") == FileState{});
    std::filesystem::remove(path);
    std::filesystem::remove(binary);
}

namespace {

struct ViewFixture {
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
};

} // namespace

TEST_CASE("The *acp review* buffer shows a turn's hunks and undoes, keeps and closes from its keys", "[AcpReview]") {
    ViewFixture                 fixture;
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned-acp-review-view.txt";
    WriteWhole(path, "a\nB\nc\n");
    TurnFile file = Changed("a\nb\nc\n", "a\nB\nc\n");
    file.path     = path;
    fixture.view.OpenAcpTurnReview("fix b", {file});

    ned::text::Buffer& review = fixture.activeBuffer.Get();
    REQUIRE(review.Name() == ned::editor::acp::kReviewBufferName);
    REQUIRE(review.ReadOnly());
    REQUIRE(review.Text().find("- b\n+ B\n") != std::string::npos);
    const ned::editor::Mode mode = ned::editor::CachedModeForBuffer(review);
    REQUIRE(mode.keymap.Resolve(ned::editor::ParseKeySequence("u")).commandName == "acp-review-undo-hunk");
    REQUIRE(mode.keymap.Resolve(ned::editor::ParseKeySequence("q")).commandName == "acp-review-quit");

    fixture.view.HandleAcpReviewRequest(ned::editor::InteractiveRequest::AcpReviewKeep);
    REQUIRE(fixture.activeBuffer.Get().Text().find("✓ kept") != std::string::npos);

    fixture.view.HandleAcpReviewRequest(ned::editor::InteractiveRequest::AcpReviewUndoHunk);
    REQUIRE(ReadWhole(path) == "a\nb\nc\n");
    REQUIRE(fixture.activeBuffer.Get().Name() == ned::editor::acp::kReviewBufferName);
    REQUIRE(fixture.activeBuffer.Get().Text().find("↶ undone") != std::string::npos);

    fixture.view.HandleAcpReviewRequest(ned::editor::InteractiveRequest::AcpReviewQuit);
    REQUIRE(fixture.bufferList.Find(std::string(ned::editor::acp::kReviewBufferName)) == nullptr);
    std::filesystem::remove(path);
}

TEST_CASE("A turn's review line opens its review", "[AcpReview]") {
    std::vector<ned::editor::acp::Manager::TranscriptEntry> transcript;
    transcript.push_back({.kind   = ned::editor::acp::Manager::TranscriptEntry::Kind::SessionEvent,
                          .text   = "2 files changed (+3 −1)",
                          .status = "review"});
    const auto lines = ned::ui::acppanel::FormatTranscript(transcript, std::nullopt, {.width = 60});
    REQUIRE(lines.size() == 1);
    REQUIRE(lines[0].text == "✎ 2 files changed (+3 −1) · review (C-c C-r)");
    REQUIRE(lines[0].action == ned::ui::acppanel::LineAction::Review);
}
