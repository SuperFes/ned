#include "UndoTree.h"

#include <utility>

namespace ned::text {

UndoTree::UndoTree(Rope initialState) {
    root_         = std::make_unique<Node>();
    root_->state  = std::move(initialState);
    current_      = root_.get();
}

const Rope& UndoTree::Current() const {
    return current_->state;
}

void UndoTree::Record(Rope newState) {
    auto child    = std::make_unique<Node>();
    child->state  = std::move(newState);
    child->parent = current_;

    Node* childPtr = child.get();

    current_->children.push_back(std::move(child));
    current_->mostRecentChild = current_->children.size() - 1;
    current_                  = childPtr;
}

void UndoTree::Amend(Rope newState) {
    current_->state = std::move(newState);
}

bool UndoTree::CanUndo() const {
    return current_->parent != nullptr;
}

bool UndoTree::CanRedo() const {
    return !current_->children.empty();
}

void UndoTree::Undo() {
    if (!CanUndo()) {
        return;
    }
    current_ = current_->parent;
}

void UndoTree::Redo() {
    if (!CanRedo()) {
        return;
    }
    current_ = current_->children[current_->mostRecentChild].get();
}

std::size_t UndoTree::ChildCount() const {
    return current_->children.size();
}

} // namespace ned::text
