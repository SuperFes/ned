#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

#include "Editor/ProjectUndo.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"

using ned::editor::ProjectEditTransaction;
using ned::editor::ProjectUndoManager;
using ned::editor::ProjectUndoOutcome;
using ned::editor::ProjectUndoRecord;
using ned::text::Buffer;
using ned::text::BufferList;

namespace {

// Simulates one file's half of a multi-file edit exactly the way
// BufferView::ApplyProjectEdit does: one undo group, before/after
// sequences captured around it.
ProjectUndoRecord ApplyOneFileEdit(Buffer& buffer, const std::string& text) {
    const std::size_t before = buffer.CurrentUndoSequence();
    buffer.BeginUndoGroup();
    buffer.SetPoint(buffer.Size());
    buffer.InsertAtPoint(text);
    buffer.EndUndoGroup();
    return ProjectUndoRecord{
        .path           = *buffer.Path(),
        .beforeSequence = before,
        .afterSequence  = buffer.CurrentUndoSequence(),
    };
}

} // namespace

TEST_CASE("RecordTransaction drops a transaction touching fewer than two files", "[ProjectUndo]") {
    BufferList bufferList;
    Buffer&    a = bufferList.OpenOrCreateFile(std::filesystem::temp_directory_path() / "project-undo-single.txt");

    ProjectEditTransaction transaction;
    transaction.description = "single-file edit";
    transaction.records.push_back(ApplyOneFileEdit(a, "x"));

    ProjectUndoManager manager;
    manager.RecordTransaction(std::move(transaction));

    REQUIRE_FALSE(manager.CanUndo());
    REQUIRE_FALSE(manager.IsUndoTarget(a));
}

TEST_CASE("Undo/Redo roll every touched file back and forward together", "[ProjectUndo]") {
    BufferList bufferList;
    Buffer&    a = bufferList.OpenOrCreateFile(std::filesystem::temp_directory_path() / "project-undo-a.txt");
    Buffer&    b = bufferList.OpenOrCreateFile(std::filesystem::temp_directory_path() / "project-undo-b.txt");

    ProjectEditTransaction transaction;
    transaction.description = "Renamed (2 files)";
    transaction.records.push_back(ApplyOneFileEdit(a, "foo"));
    transaction.records.push_back(ApplyOneFileEdit(b, "foo"));
    REQUIRE(a.Text() == "foo");
    REQUIRE(b.Text() == "foo");

    ProjectUndoManager manager;
    manager.RecordTransaction(std::move(transaction));
    REQUIRE(manager.CanUndo());
    REQUIRE_FALSE(manager.CanRedo());
    REQUIRE(manager.IsUndoTarget(a));
    REQUIRE(manager.IsUndoTarget(b));

    const ProjectUndoOutcome undone = manager.Undo(bufferList);
    REQUIRE(undone.description == "Renamed (2 files)");
    REQUIRE(undone.totalCount == 2);
    REQUIRE(undone.appliedCount == 2);
    REQUIRE(undone.divergedNames.empty());
    REQUIRE(a.Text().empty());
    REQUIRE(b.Text().empty());

    REQUIRE_FALSE(manager.CanUndo());
    REQUIRE(manager.CanRedo());
    REQUIRE(manager.IsRedoTarget(a));
    REQUIRE(manager.IsRedoTarget(b));

    const ProjectUndoOutcome redone = manager.Redo(bufferList);
    REQUIRE(redone.appliedCount == 2);
    REQUIRE(a.Text() == "foo");
    REQUIRE(b.Text() == "foo");
}

TEST_CASE("Undo skips a file edited separately since the transaction, without blocking the rest", "[ProjectUndo]") {
    BufferList bufferList;
    Buffer&    a = bufferList.OpenOrCreateFile(std::filesystem::temp_directory_path() / "project-undo-diverge-a.txt");
    Buffer&    b = bufferList.OpenOrCreateFile(std::filesystem::temp_directory_path() / "project-undo-diverge-b.txt");

    ProjectEditTransaction transaction;
    transaction.description = "Renamed (2 files)";
    transaction.records.push_back(ApplyOneFileEdit(a, "foo"));
    transaction.records.push_back(ApplyOneFileEdit(b, "foo"));

    ProjectUndoManager manager;
    manager.RecordTransaction(std::move(transaction));

    // An unrelated edit lands on `a` after the transaction -- its current
    // sequence no longer matches what the transaction expects.
    a.SetPoint(a.Size());
    a.InsertAtPoint("!");
    REQUIRE_FALSE(manager.IsUndoTarget(a)); // no longer exactly on the transaction's edge
    REQUIRE(manager.IsUndoTarget(b));       // b is untouched, still is

    const ProjectUndoOutcome outcome = manager.Undo(bufferList);
    REQUIRE(outcome.totalCount == 2);
    REQUIRE(outcome.appliedCount == 1);
    REQUIRE(outcome.divergedNames.size() == 1);
    REQUIRE(outcome.divergedNames.front() == a.Name());
    REQUIRE(a.Text() == "foo!"); // left exactly as the separate edit left it
    REQUIRE(b.Text().empty());   // still rolled back
}

TEST_CASE("Undo reports a closed file as diverged rather than dangling", "[ProjectUndo]") {
    BufferList        bufferList;
    Buffer&           a     = bufferList.OpenOrCreateFile(std::filesystem::temp_directory_path() / "project-undo-closed-a.txt");
    Buffer&           b     = bufferList.OpenOrCreateFile(std::filesystem::temp_directory_path() / "project-undo-closed-b.txt");
    const std::string aName = a.Name();

    ProjectEditTransaction transaction;
    transaction.description = "Renamed (2 files)";
    transaction.records.push_back(ApplyOneFileEdit(a, "foo"));
    transaction.records.push_back(ApplyOneFileEdit(b, "foo"));

    ProjectUndoManager manager;
    manager.RecordTransaction(std::move(transaction));

    REQUIRE(bufferList.Close(aName));

    const ProjectUndoOutcome outcome = manager.Undo(bufferList);
    REQUIRE(outcome.appliedCount == 1);
    REQUIRE(outcome.divergedNames.size() == 1);
    REQUIRE(b.Text().empty());
}

TEST_CASE("Undo on an empty stack is a no-op outcome", "[ProjectUndo]") {
    BufferList               bufferList;
    ProjectUndoManager       manager;
    const ProjectUndoOutcome outcome = manager.Undo(bufferList);
    REQUIRE(outcome.totalCount == 0);
    REQUIRE(outcome.appliedCount == 0);
    REQUIRE(outcome.divergedNames.empty());
}

TEST_CASE("Recording a new transaction clears the redo stack", "[ProjectUndo]") {
    BufferList bufferList;
    Buffer&    a = bufferList.OpenOrCreateFile(std::filesystem::temp_directory_path() / "project-undo-clear-a.txt");
    Buffer&    b = bufferList.OpenOrCreateFile(std::filesystem::temp_directory_path() / "project-undo-clear-b.txt");

    ProjectEditTransaction first;
    first.description = "first";
    first.records.push_back(ApplyOneFileEdit(a, "foo"));
    first.records.push_back(ApplyOneFileEdit(b, "foo"));

    ProjectUndoManager manager;
    manager.RecordTransaction(std::move(first));
    manager.Undo(bufferList);
    REQUIRE(manager.CanRedo());

    ProjectEditTransaction second;
    second.description = "second";
    second.records.push_back(ApplyOneFileEdit(a, "bar"));
    second.records.push_back(ApplyOneFileEdit(b, "bar"));
    manager.RecordTransaction(std::move(second));

    REQUIRE_FALSE(manager.CanRedo());
}
