#include "ProjectUndo.h"

#include "Text/Buffer.h"

namespace ned::editor {

namespace {

    [[nodiscard]] bool IsTarget(const std::deque<ProjectEditTransaction>& stack, const text::Buffer& buffer, bool wantAfterSequence) {
        if (stack.empty() || !buffer.Path()) {
            return false;
        }
        const std::size_t current = buffer.CurrentUndoSequence();
        for (const ProjectUndoRecord& record : stack.back().records) {
            if (record.path != *buffer.Path()) {
                continue;
            }
            return current == (wantAfterSequence ? record.afterSequence : record.beforeSequence);
        }
        return false;
    }

} // namespace

void ProjectUndoManager::RecordTransaction(ProjectEditTransaction transaction) {
    if (transaction.records.size() < 2) {
        return;
    }
    undoStack_.push_back(std::move(transaction));
    redoStack_.clear();
}

bool ProjectUndoManager::CanUndo() const {
    return !undoStack_.empty();
}

bool ProjectUndoManager::CanRedo() const {
    return !redoStack_.empty();
}

bool ProjectUndoManager::IsUndoTarget(const text::Buffer& buffer) const {
    return IsTarget(undoStack_, buffer, /*wantAfterSequence=*/true);
}

bool ProjectUndoManager::IsRedoTarget(const text::Buffer& buffer) const {
    return IsTarget(redoStack_, buffer, /*wantAfterSequence=*/false);
}

ProjectUndoOutcome ProjectUndoManager::Undo(text::BufferList& bufferList) {
    ProjectUndoOutcome outcome;
    if (undoStack_.empty()) {
        return outcome;
    }
    ProjectEditTransaction transaction = std::move(undoStack_.back());
    undoStack_.pop_back();

    outcome.description = transaction.description;
    outcome.totalCount  = transaction.records.size();
    for (const ProjectUndoRecord& record : transaction.records) {
        text::Buffer* buffer = bufferList.FindByPath(record.path);
        if (!buffer || buffer->CurrentUndoSequence() != record.afterSequence || !buffer->TryJumpToUndoSequence(record.beforeSequence)) {
            outcome.divergedNames.push_back(buffer ? buffer->Name() : record.path.filename().string());
            continue;
        }
        ++outcome.appliedCount;
    }

    redoStack_.push_back(std::move(transaction));
    return outcome;
}

ProjectUndoOutcome ProjectUndoManager::Redo(text::BufferList& bufferList) {
    ProjectUndoOutcome outcome;
    if (redoStack_.empty()) {
        return outcome;
    }
    ProjectEditTransaction transaction = std::move(redoStack_.back());
    redoStack_.pop_back();

    outcome.description = transaction.description;
    outcome.totalCount  = transaction.records.size();
    for (const ProjectUndoRecord& record : transaction.records) {
        text::Buffer* buffer = bufferList.FindByPath(record.path);
        if (!buffer || buffer->CurrentUndoSequence() != record.beforeSequence || !buffer->TryJumpToUndoSequence(record.afterSequence)) {
            outcome.divergedNames.push_back(buffer ? buffer->Name() : record.path.filename().string());
            continue;
        }
        ++outcome.appliedCount;
    }

    undoStack_.push_back(std::move(transaction));
    return outcome;
}

} // namespace ned::editor
