#include "Tree.h"

#include <utility>

namespace ned::editor::grammar {

Tree::Tree(parse::GreenTree tree) noexcept : tree_(std::move(tree)) {
}

Tree::~Tree() = default;

Tree::Tree(Tree&& other) noexcept : tree_(std::move(other.tree_)) {
    other.tree_ = parse::GreenTree{};
}

Tree& Tree::operator=(Tree&& other) noexcept {
    if (this != &other) {
        tree_       = std::move(other.tree_);
        other.tree_ = parse::GreenTree{};
    }
    return *this;
}

bool Tree::IsNull() const noexcept {
    return tree_.IsNull();
}

Node Tree::RootNode() const {
    return Node(tree_.RootNode());
}

void Tree::Edit(const parse::InputEdit& edit) noexcept {
    if (tree_.IsNull())
        return;
    tree_ = tree_.WithEdit(edit);
}

const parse::GreenTree& Tree::Green() const noexcept {
    return tree_;
}

Tree Tree::Clone() const {
    return Tree(tree_);
}

} // namespace ned::editor::grammar
