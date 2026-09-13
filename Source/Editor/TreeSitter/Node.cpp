#include "Node.h"

namespace ned::editor::treesitter {

Node::Node(parse::RedNode node) noexcept : node_(node) {
}

std::string_view Node::Type() const {
    return parse::NodeType(node_);
}

std::size_t Node::StartByte() const {
    return parse::NodeStartByte(node_);
}

std::size_t Node::EndByte() const {
    return parse::NodeEndByte(node_);
}

std::size_t Node::StartRow() const {
    return parse::NodeStartPoint(node_).row;
}

std::size_t Node::StartColumn() const {
    return parse::NodeStartPoint(node_).column;
}

std::size_t Node::ChildCount() const {
    return parse::NodeChildCount(node_);
}

Node Node::Child(std::size_t index) const {
    return Node(parse::NodeChild(node_, static_cast<uint32_t>(index)));
}

bool Node::IsNamed() const {
    return parse::NodeIsNamed(node_);
}

bool Node::IsExtra() const {
    return parse::NodeIsExtra(node_);
}

Node Node::ChildByFieldName(std::string_view fieldName) const {
    return Node(parse::NodeChildByFieldName(node_, fieldName.data(), static_cast<uint32_t>(fieldName.size())));
}

Node Node::Parent() const {
    return Node(parse::NodeParent(node_));
}

Node Node::NextNamedSibling() const {
    return Node(parse::NodeNextNamedSibling(node_));
}

Node Node::PrevNamedSibling() const {
    return Node(parse::NodePrevNamedSibling(node_));
}

Node Node::NamedDescendantForByteRange(std::size_t start, std::size_t end) const {
    return Node(parse::NodeNamedDescendantForByteRange(node_, static_cast<uint32_t>(start), static_cast<uint32_t>(end)));
}

Node Node::DescendantForByteRange(std::size_t start, std::size_t end) const {
    return Node(parse::NodeDescendantForByteRange(node_, static_cast<uint32_t>(start), static_cast<uint32_t>(end)));
}

bool Node::IsNull() const {
    return parse::NodeIsNull(node_);
}

parse::RedNode Node::Raw() const noexcept {
    return node_;
}

const void* Node::Id() const noexcept {
    return node_.id;
}

} // namespace ned::editor::treesitter
