#include "Tree.h"

#include <stdexcept>
#include <utility>

namespace ned::editor::treesitter {

Tree::Tree(TSTree* tree) noexcept : tree_(tree) {
}

Tree::~Tree() {
    ts_tree_delete(tree_); // ts_tree_delete(nullptr) is a documented no-op
}

Tree::Tree(Tree&& other) noexcept : tree_(std::exchange(other.tree_, nullptr)) {
}

Tree& Tree::operator=(Tree&& other) noexcept {
    if (this != &other) {
        ts_tree_delete(tree_);
        tree_ = std::exchange(other.tree_, nullptr);
    }
    return *this;
}

bool Tree::IsNull() const noexcept {
    return tree_ == nullptr;
}

Node Tree::RootNode() const {
    if (IsNull()) {
        throw std::runtime_error("ned: Tree::RootNode() called on a null tree");
    }
    return Node(ts_tree_root_node(tree_));
}

void Tree::Edit(const TSInputEdit& edit) noexcept {
    if (!IsNull()) {
        ts_tree_edit(tree_, &edit);
    }
}

const TSTree* Tree::Raw() const noexcept {
    return tree_;
}

} // namespace ned::editor::treesitter
