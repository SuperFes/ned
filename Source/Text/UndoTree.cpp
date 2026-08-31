#include "UndoTree.h"

#include <functional>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#include "Rope.h"
#include "RopeStorage.h"

namespace ned::text {

UndoTree::UndoTree(std::unique_ptr<ITextStorage> initialState) {
    root_        = std::make_unique<Node>();
    root_->state = std::move(initialState);
    current_     = root_.get();
}

const ITextStorage& UndoTree::Current() const {
    return *current_->state;
}

void UndoTree::Record(std::unique_ptr<ITextStorage> newState) {
    auto child    = std::make_unique<Node>();
    child->state  = std::move(newState);
    child->parent = current_;

    Node* childPtr = child.get();

    current_->children.push_back(std::move(child));
    current_->mostRecentChild = current_->children.size() - 1;
    current_                  = childPtr;
}

void UndoTree::Amend(std::unique_ptr<ITextStorage> newState) {
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

std::vector<UndoTree::SerializedNode> UndoTree::Serialize() const {
    std::vector<SerializedNode> out;

    std::function<void(const Node*, std::optional<std::size_t>)> walk = [&](const Node* node, std::optional<std::size_t> parentId) {
        const std::size_t id = out.size();
        out.push_back(SerializedNode{id, parentId, node->state->ToString(), node->mostRecentChild});
        for (const auto& child : node->children) {
            walk(child.get(), id);
        }
    };
    walk(root_.get(), std::nullopt);
    return out;
}

std::size_t UndoTree::CurrentNodeId() const {
    std::size_t id    = 0;
    std::size_t found = 0;

    std::function<bool(const Node*)> walk = [&](const Node* node) -> bool {
        const std::size_t thisId = id++;
        if (node == current_) {
            found = thisId;
            return true;
        }
        for (const auto& child : node->children) {
            if (walk(child.get())) {
                return true;
            }
        }
        return false;
    };
    walk(root_.get());
    return found;
}

UndoTree UndoTree::Deserialize(const std::vector<SerializedNode>& nodes, std::size_t currentId) {
    if (nodes.empty()) {
        throw std::runtime_error("UndoTree::Deserialize: empty node list");
    }

    std::unordered_map<std::size_t, const SerializedNode*> byId;
    byId.reserve(nodes.size());
    for (const auto& node : nodes) {
        if (!byId.emplace(node.id, &node).second) {
            throw std::runtime_error("UndoTree::Deserialize: duplicate node id");
        }
    }

    std::optional<std::size_t> rootId;
    for (const auto& node : nodes) {
        if (!node.parentId) {
            if (rootId) {
                throw std::runtime_error("UndoTree::Deserialize: more than one root");
            }
            rootId = node.id;
        }
    }
    if (!rootId) {
        throw std::runtime_error("UndoTree::Deserialize: no root");
    }
    if (!byId.contains(currentId)) {
        throw std::runtime_error("UndoTree::Deserialize: unknown currentId");
    }

    // Validate every node's ancestor chain actually reaches the root within
    // a bounded number of steps -- catches a dangling parentId and a cycle
    // alike, before any ownership moves below make a cycle unrecoverable
    // (two nodes pointing to each other as parent would otherwise become a
    // real std::unique_ptr ownership cycle -- a silent leak, not a crash).
    for (const auto& node : nodes) {
        const SerializedNode* walker = &node;
        for (std::size_t steps = 0; walker->id != *rootId; ++steps) {
            if (steps > nodes.size()) {
                throw std::runtime_error("UndoTree::Deserialize: cycle in parent chain");
            }
            if (!walker->parentId) {
                // Only the root may lack a parentId; reaching a second
                // parentless node mid-walk means a disconnected component.
                throw std::runtime_error("UndoTree::Deserialize: node unreachable from root");
            }
            const auto parentIt = byId.find(*walker->parentId);
            if (parentIt == byId.end()) {
                throw std::runtime_error("UndoTree::Deserialize: dangling parentId");
            }
            walker = parentIt->second;
        }
    }

    std::unordered_map<std::size_t, std::unique_ptr<Node>> owned;
    std::unordered_map<std::size_t, Node*>                 live;
    owned.reserve(nodes.size());
    live.reserve(nodes.size());
    for (const auto& node : nodes) {
        auto n             = std::make_unique<Node>();
        // Deserialize always reconstructs a Rope-backed snapshot -- see
        // SerializedNode's own doc comment on why that's always correct
        // here (a huge/piece-table-backed buffer never reaches this path
        // in the first place).
        n->state           = std::make_unique<RopeStorage>(Rope(node.content));
        n->mostRecentChild = node.mostRecentChild;
        live[node.id]      = n.get();
        owned[node.id]     = std::move(n);
    }
    for (const auto& node : nodes) {
        if (!node.parentId) {
            continue;
        }
        Node* parent = live.at(*node.parentId);
        Node* self   = live.at(node.id);
        self->parent = parent;
        parent->children.push_back(std::move(owned.at(node.id)));
    }

    UndoTree tree{std::make_unique<RopeStorage>()};
    tree.root_    = std::move(owned.at(*rootId));
    tree.current_ = live.at(currentId);
    return tree;
}

} // namespace ned::text
