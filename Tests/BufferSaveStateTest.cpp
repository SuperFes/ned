// Buffer's save state machine (BeginSave / ExecuteSavePlan / FinishSave),
// driven step by step the way an asynchronous save will drive it -- edits
// land between BeginSave and FinishSave here, which is exactly what a write
// running on another thread makes possible and what the old
// save-then-clear-everything sequence would have silently discarded.
//
// No threads: the ordering is what matters, and stepping it explicitly
// tests it deterministically.

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

#include "Text/Buffer.h"
#include "Text/SavePlan.h"

using ned::text::Buffer;
using ned::text::ExecuteSavePlan;
using ned::text::SavePlan;

namespace {

std::filesystem::path TempPath(const std::string& name) {
    return std::filesystem::temp_directory_path() / ("ned_save_state_" + name + "_" + std::to_string(::getpid()));
}

std::string ReadFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

Buffer BufferWith(std::string_view text) {
    Buffer buffer("save-state-test");
    buffer.InsertAt(0, text);
    return buffer;
}

} // namespace

TEST_CASE("A buffer is not saving until BeginSave and not saving again after FinishSave", "[BufferSaveState]") {
    const std::filesystem::path path   = TempPath("flag.txt");
    Buffer                      buffer = BufferWith("content\n");

    REQUIRE_FALSE(buffer.IsSaving());

    SavePlan plan = buffer.BeginSave(path, /*ensureFinalNewline=*/false, /*trimTrailingWhitespace=*/false);
    REQUIRE(buffer.IsSaving());

    ExecuteSavePlan(plan);
    buffer.FinishSave(path, std::move(plan));

    REQUIRE_FALSE(buffer.IsSaving());
    REQUIRE_FALSE(buffer.Modified());
    REQUIRE(ReadFile(path) == "content\n");
    std::filesystem::remove(path);
}

TEST_CASE("BeginSave refuses a second save while one is in flight", "[BufferSaveState]") {
    const std::filesystem::path path   = TempPath("double.txt");
    Buffer                      buffer = BufferWith("content\n");

    SavePlan plan = buffer.BeginSave(path, false, false);
    REQUIRE_THROWS_AS(buffer.BeginSave(path, false, false), std::runtime_error);

    ExecuteSavePlan(plan);
    buffer.FinishSave(path, std::move(plan));
    std::filesystem::remove(path);
}

TEST_CASE("An edit made while a save is in flight leaves the buffer modified afterwards", "[BufferSaveState]") {
    const std::filesystem::path path   = TempPath("midedit.txt");
    Buffer                      buffer = BufferWith("original\n");

    SavePlan plan = buffer.BeginSave(path, false, false);

    // Stands in for a keystroke arriving while the write runs on another
    // thread. Clearing the unsaved set on completion would report this
    // buffer as matching disk when it plainly does not.
    buffer.InsertAt(0, "EDIT ");

    ExecuteSavePlan(plan);
    buffer.FinishSave(path, std::move(plan));

    REQUIRE(buffer.Modified());
    REQUIRE_FALSE(buffer.UnsavedChangeRanges().empty());
    // The file holds what was captured at BeginSave, not the later edit.
    REQUIRE(ReadFile(path) == "original\n");
    REQUIRE(buffer.Text() == "EDIT original\n");
    std::filesystem::remove(path);
}

TEST_CASE("Only the edits made during the save stay marked unsaved", "[BufferSaveState]") {
    const std::filesystem::path path   = TempPath("scoped.txt");
    Buffer                      buffer = BufferWith("aaa\nbbb\nccc\n");

    // A pre-save edit: written by this save, so it must not survive it.
    buffer.InsertAt(0, "X");
    REQUIRE(buffer.Modified());

    SavePlan          plan         = buffer.BeginSave(path, false, false);
    const std::size_t rangesBefore = buffer.UnsavedChangeRanges().size();
    REQUIRE(rangesBefore > 0);

    ExecuteSavePlan(plan);
    buffer.FinishSave(path, std::move(plan));

    // Nothing was edited during the save, so the pre-save edit is now saved.
    REQUIRE_FALSE(buffer.Modified());
    REQUIRE(buffer.UnsavedChangeRanges().empty());
    REQUIRE(ReadFile(path) == "Xaaa\nbbb\nccc\n");
    std::filesystem::remove(path);
}

TEST_CASE("A failed save abandons cleanly and keeps every unsaved edit marked", "[BufferSaveState]") {
    // A directory that does not exist: ExecuteSavePlan throws, which is the
    // path SaveToFile turns into AbandonSave.
    const std::filesystem::path path   = TempPath("nodir") / "nested" / "file.txt";
    Buffer                      buffer = BufferWith("content\n");
    buffer.InsertAt(0, "unsaved ");

    REQUIRE_THROWS_AS(buffer.SaveToFile(path, false, false), std::runtime_error);

    REQUIRE_FALSE(buffer.IsSaving());
    REQUIRE(buffer.Modified());
    REQUIRE_FALSE(buffer.UnsavedChangeRanges().empty());

    // And the buffer is left able to save again, rather than stuck.
    const std::filesystem::path good = TempPath("recovered.txt");
    REQUIRE_NOTHROW(buffer.SaveToFile(good, false, false));
    REQUIRE_FALSE(buffer.Modified());
    std::filesystem::remove(good);
}

TEST_CASE("A deletion made while a save is in flight stays marked unsaved", "[BufferSaveState]") {
    const std::filesystem::path path   = TempPath("middelete.txt");
    Buffer                      buffer = BufferWith("keep this text\n");

    SavePlan plan = buffer.BeginSave(path, false, false);
    buffer.DeleteRange(0, 5); // "keep "

    ExecuteSavePlan(plan);
    buffer.FinishSave(path, std::move(plan));

    REQUIRE(buffer.Modified());
    REQUIRE(ReadFile(path) == "keep this text\n");
    REQUIRE(buffer.Text() == "this text\n");
    std::filesystem::remove(path);
}

TEST_CASE("A buffer whose save is in flight does not count as needing a quit prompt", "[BufferSaveState]") {
    const std::filesystem::path path   = TempPath("pendingquit.txt");
    Buffer                      buffer = BufferWith("content\n");

    REQUIRE(buffer.Modified());
    REQUIRE(buffer.ModifiedAfterPendingSave());

    SavePlan plan = buffer.BeginSave(path, false, false);

    // Right now it is still Modified() -- the bytes aren't on disk yet --
    // but the save that will put them there is already running, so quitting
    // must not prompt about it.
    REQUIRE(buffer.Modified());
    REQUIRE_FALSE(buffer.ModifiedAfterPendingSave());

    ExecuteSavePlan(plan);
    buffer.FinishSave(path, std::move(plan));

    REQUIRE_FALSE(buffer.ModifiedAfterPendingSave());
    std::filesystem::remove(path);
}

TEST_CASE("A buffer edited during its save does count as needing a quit prompt", "[BufferSaveState]") {
    const std::filesystem::path path   = TempPath("pendingquitedit.txt");
    Buffer                      buffer = BufferWith("content\n");

    SavePlan plan = buffer.BeginSave(path, false, false);
    REQUIRE_FALSE(buffer.ModifiedAfterPendingSave());

    buffer.InsertAt(0, "EDIT "); // genuinely would be lost on quit
    REQUIRE(buffer.ModifiedAfterPendingSave());

    ExecuteSavePlan(plan);
    buffer.FinishSave(path, std::move(plan));

    REQUIRE(buffer.ModifiedAfterPendingSave());
    std::filesystem::remove(path);
}

TEST_CASE("A buffer emptied during its save still counts as needing a quit prompt", "[BufferSaveState]") {
    const std::filesystem::path path   = TempPath("pendingquitempty.txt");
    Buffer                      buffer = BufferWith("content\n");

    SavePlan plan = buffer.BeginSave(path, false, false);
    // Deleting everything leaves no byte anywhere to mark a range against
    // -- the one case Modified() itself has to answer by length.
    buffer.DeleteRange(0, buffer.Size());
    REQUIRE(buffer.ModifiedAfterPendingSave());

    ExecuteSavePlan(plan);
    buffer.FinishSave(path, std::move(plan));
    std::filesystem::remove(path);
}

TEST_CASE("Save progress is exposed while saving and cleared when it finishes", "[BufferSaveState]") {
    const std::filesystem::path path   = TempPath("progress.txt");
    Buffer                      buffer = BufferWith("content\n");

    REQUIRE(buffer.CurrentSaveProgress() == nullptr);

    SavePlan plan        = buffer.BeginSave(path, false, false);
    auto     progress    = std::make_shared<ned::text::SaveProgress>();
    progress->totalBytes = buffer.Size();
    buffer.SetSaveProgress(progress);

    REQUIRE(buffer.CurrentSaveProgress() != nullptr);
    REQUIRE(buffer.CurrentSaveProgress()->totalBytes == buffer.Size());

    ExecuteSavePlan(plan);
    buffer.FinishSave(path, std::move(plan));

    REQUIRE(buffer.CurrentSaveProgress() == nullptr);
    std::filesystem::remove(path);
}
