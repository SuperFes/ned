#include "Node.h"

namespace ned::editor::treesitter {

Node::Node(TSNode node) noexcept : node_(node) {
}

std::string_view Node::Type() const {
    return ts_node_type(node_);
}

std::size_t Node::StartByte() const {
    return ts_node_start_byte(node_);
}

std::size_t Node::EndByte() const {
    return ts_node_end_byte(node_);
}

std::size_t Node::StartRow() const {
    return ts_node_start_point(node_).row;
}

std::size_t Node::StartColumn() const {
    return ts_node_start_point(node_).column;
}

std::size_t Node::ChildCount() const {
    return ts_node_child_count(node_);
}

Node Node::Child(std::size_t index) const {
    return Node(ts_node_child(node_, static_cast<uint32_t>(index)));
}

bool Node::IsNamed() const {
    return ts_node_is_named(node_);
}

Node Node::Parent() const {
    return Node(ts_node_parent(node_));
}

Node Node::NextNamedSibling() const {
    return Node(ts_node_next_named_sibling(node_));
}

Node Node::PrevNamedSibling() const {
    return Node(ts_node_prev_named_sibling(node_));
}

Node Node::NamedDescendantForByteRange(std::size_t start, std::size_t end) const {
    return Node(ts_node_named_descendant_for_byte_range(node_, static_cast<uint32_t>(start), static_cast<uint32_t>(end)));
}

Node Node::DescendantForByteRange(std::size_t start, std::size_t end) const {
    return Node(ts_node_descendant_for_byte_range(node_, static_cast<uint32_t>(start), static_cast<uint32_t>(end)));
}

bool Node::IsNull() const {
    return ts_node_is_null(node_);
}

TSNode Node::Raw() const noexcept {
    return node_;
}

const void* Node::Id() const noexcept {
    return node_.id;
}

} // namespace ned::editor::treesitter
