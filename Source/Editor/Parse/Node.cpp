#include "Editor/Parse/Node.h"

#include "Editor/Parse/LanguageTables.h"

namespace ned::editor::parse {

namespace {

    struct NodeChildIterator {
        Subtree            parent;
        const TreeData*    tree;
        Length             position;
        std::uint32_t      childIndex;
        std::uint32_t      structuralChildIndex;
        const abi::Symbol* aliasSequence;
    };

    abi::Symbol NodeAlias(const RedNode* self) {
        return static_cast<abi::Symbol>(self->context[3]);
    }

    Subtree NodeSubtree(RedNode self) {
        return *self.id;
    }

    NodeChildIterator IterateChildren(const RedNode* node) {
        const Subtree subtree = NodeSubtree(*node);
        if (SubtreeChildCount(subtree) == 0)
            return {kNullSubtree, node->tree, LengthZero(), 0, 0, nullptr};
        const abi::Symbol* aliasSequence = LanguageAliasSequence(node->tree->language, subtree.ptr->productionId);
        return {
            .parent               = subtree,
            .tree                 = node->tree,
            .position             = {NodeStartByte(*node), NodeStartPoint(*node)},
            .childIndex           = 0,
            .structuralChildIndex = 0,
            .aliasSequence        = aliasSequence,
        };
    }

    bool ChildIteratorNext(NodeChildIterator* self, RedNode* result) {
        if (self->parent.ptr == nullptr || self->childIndex == self->parent.ptr->childCount)
            return false;
        const Subtree* child       = &SubtreeChildren(self->parent)[self->childIndex];
        abi::Symbol    aliasSymbol = 0;
        if (!SubtreeExtra(*child)) {
            if (self->aliasSequence != nullptr)
                aliasSymbol = self->aliasSequence[self->structuralChildIndex];
            self->structuralChildIndex++;
        }
        if (self->childIndex > 0)
            self->position = LengthAdd(self->position, SubtreePadding(*child));
        *result        = NodeNew(self->tree, child, self->position, aliasSymbol);
        self->position = LengthAdd(self->position, SubtreeSize(*child));
        self->childIndex++;
        return true;
    }

    bool NodeIsRelevant(RedNode self, bool includeAnonymous) {
        const Subtree tree = NodeSubtree(self);
        if (includeAnonymous)
            return SubtreeVisible(tree) || NodeAlias(&self) != 0;
        const abi::Symbol alias = NodeAlias(&self);
        if (alias != 0)
            return LanguageSymbolMetadata(self.tree->language, alias).named;
        return SubtreeVisible(tree) && SubtreeNamed(tree);
    }

    std::uint32_t NodeRelevantChildCount(RedNode self, bool includeAnonymous) {
        const Subtree tree = NodeSubtree(self);
        if (SubtreeChildCount(tree) > 0)
            return includeAnonymous ? tree.ptr->visibleChildCount : tree.ptr->namedChildCount;
        return 0;
    }

    RedNode NodeChildImpl(RedNode self, std::uint32_t childIndex, bool includeAnonymous) {
        RedNode result     = self;
        bool    didDescend = true;

        while (didDescend) {
            didDescend = false;

            RedNode           child;
            std::uint32_t     index    = 0;
            NodeChildIterator iterator = IterateChildren(&result);
            while (ChildIteratorNext(&iterator, &child)) {
                if (NodeIsRelevant(child, includeAnonymous)) {
                    if (index == childIndex)
                        return child;
                    index++;
                }
                else {
                    const std::uint32_t grandchildIndex = childIndex - index;
                    const std::uint32_t grandchildCount = NodeRelevantChildCount(child, includeAnonymous);
                    if (grandchildIndex < grandchildCount) {
                        didDescend = true;
                        result     = child;
                        childIndex = grandchildIndex;
                        break;
                    }
                    index += grandchildCount;
                }
            }
        }

        return NodeNull();
    }

    bool SubtreeHasTrailingEmptyDescendant(Subtree self, Subtree other) {
        for (unsigned i = SubtreeChildCount(self) - 1; i + 1 > 0; i--) {
            const Subtree child = SubtreeChildren(self)[i];
            if (SubtreeTotalBytes(child) > 0)
                break;
            if (child.ptr == other.ptr || SubtreeHasTrailingEmptyDescendant(child, other))
                return true;
        }
        return false;
    }

    RedNode NodePrevSiblingImpl(RedNode self, bool includeAnonymous) {
        const Subtree       selfSubtree   = NodeSubtree(self);
        const bool          selfIsEmpty   = SubtreeTotalBytes(selfSubtree) == 0;
        const std::uint32_t targetEndByte = NodeEndByte(self);

        RedNode node                  = NodeParent(self);
        RedNode earlierNode           = NodeNull();
        bool    earlierNodeIsRelevant = false;

        while (!NodeIsNull(node)) {
            RedNode earlierChild               = NodeNull();
            bool    earlierChildIsRelevant     = false;
            bool    foundChildContainingTarget = false;

            RedNode           child;
            NodeChildIterator iterator = IterateChildren(&node);
            while (ChildIteratorNext(&iterator, &child)) {
                if (child.id == self.id)
                    break;
                if (iterator.position.bytes > targetEndByte) {
                    foundChildContainingTarget = true;
                    break;
                }

                if (iterator.position.bytes == targetEndByte &&
                    (!selfIsEmpty || SubtreeHasTrailingEmptyDescendant(NodeSubtree(child), selfSubtree))) {
                    foundChildContainingTarget = true;
                    break;
                }

                if (NodeIsRelevant(child, includeAnonymous)) {
                    earlierChild           = child;
                    earlierChildIsRelevant = true;
                }
                else if (NodeRelevantChildCount(child, includeAnonymous) > 0) {
                    earlierChild           = child;
                    earlierChildIsRelevant = false;
                }
            }

            if (foundChildContainingTarget) {
                if (!NodeIsNull(earlierChild)) {
                    earlierNode           = earlierChild;
                    earlierNodeIsRelevant = earlierChildIsRelevant;
                }
                node = child;
            }
            else if (earlierChildIsRelevant) {
                return earlierChild;
            }
            else if (!NodeIsNull(earlierChild)) {
                node = earlierChild;
            }
            else if (earlierNodeIsRelevant) {
                return earlierNode;
            }
            else {
                node                  = earlierNode;
                earlierNode           = NodeNull();
                earlierNodeIsRelevant = false;
            }
        }

        return NodeNull();
    }

    RedNode NodeNextSiblingImpl(RedNode self, bool includeAnonymous) {
        const std::uint32_t targetEndByte = NodeEndByte(self);

        RedNode node                = NodeParent(self);
        RedNode laterNode           = NodeNull();
        bool    laterNodeIsRelevant = false;

        while (!NodeIsNull(node)) {
            RedNode laterChild            = NodeNull();
            bool    laterChildIsRelevant  = false;
            RedNode childContainingTarget = NodeNull();

            RedNode           child;
            NodeChildIterator iterator = IterateChildren(&node);
            while (ChildIteratorNext(&iterator, &child)) {
                if (iterator.position.bytes <= targetEndByte)
                    continue;
                const std::uint32_t startByte      = NodeStartByte(self);
                const std::uint32_t childStartByte = NodeStartByte(child);

                const bool isEmpty        = startByte == targetEndByte;
                const bool containsTarget = isEmpty ? childStartByte < startByte : childStartByte <= startByte;

                if (containsTarget) {
                    if (NodeSubtree(child).ptr != NodeSubtree(self).ptr)
                        childContainingTarget = child;
                }
                else if (NodeIsRelevant(child, includeAnonymous)) {
                    laterChild           = child;
                    laterChildIsRelevant = true;
                    break;
                }
                else if (NodeRelevantChildCount(child, includeAnonymous) > 0) {
                    laterChild           = child;
                    laterChildIsRelevant = false;
                    break;
                }
            }

            if (!NodeIsNull(childContainingTarget)) {
                if (!NodeIsNull(laterChild)) {
                    laterNode           = laterChild;
                    laterNodeIsRelevant = laterChildIsRelevant;
                }
                node = childContainingTarget;
            }
            else if (laterChildIsRelevant) {
                return laterChild;
            }
            else if (!NodeIsNull(laterChild)) {
                node = laterChild;
            }
            else if (laterNodeIsRelevant) {
                return laterNode;
            }
            else {
                node = laterNode;
            }
        }

        return NodeNull();
    }

    RedNode NodeDescendantForByteRangeImpl(RedNode self, std::uint32_t rangeStart, std::uint32_t rangeEnd,
                                           bool includeAnonymous) {
        if (rangeStart > rangeEnd)
            return NodeNull();
        RedNode node            = self;
        RedNode lastVisibleNode = self;

        bool didDescend = true;
        while (didDescend) {
            didDescend = false;

            RedNode           child;
            NodeChildIterator iterator = IterateChildren(&node);
            while (ChildIteratorNext(&iterator, &child)) {
                const std::uint32_t nodeEnd = iterator.position.bytes;

                // The end of this node must extend far enough forward to touch
                // the end of the range...
                if (nodeEnd < rangeEnd)
                    continue;

                // ...and exceed the start of the range, unless the node itself
                // is empty, in which case it must at least equal the start.
                const bool isEmpty = NodeStartByte(child) == nodeEnd;
                if (isEmpty ? nodeEnd < rangeStart : nodeEnd <= rangeStart)
                    continue;

                // The start of this node must reach back to the range's start.
                if (rangeStart < NodeStartByte(child))
                    break;

                node = child;
                if (NodeIsRelevant(node, includeAnonymous))
                    lastVisibleNode = node;
                didDescend = true;
                break;
            }
        }

        return lastVisibleNode;
    }

} // namespace

RedNode NodeNew(const TreeData* tree, const Subtree* subtree, Length position, abi::Symbol alias) {
    return RedNode{{position.bytes, position.extent.row, position.extent.column, alias}, subtree, tree};
}

RedNode NodeNull() {
    return RedNode{{0, 0, 0, 0}, nullptr, nullptr};
}

bool NodeIsNull(RedNode self) {
    return self.id == nullptr;
}

std::uint32_t NodeStartByte(RedNode self) {
    return self.context[0];
}

abi::Point NodeStartPoint(RedNode self) {
    return {self.context[1], self.context[2]};
}

std::uint32_t NodeEndByte(RedNode self) {
    return NodeStartByte(self) + SubtreeSize(NodeSubtree(self)).bytes;
}

abi::Point NodeEndPoint(RedNode self) {
    return PointAdd(NodeStartPoint(self), SubtreeSize(NodeSubtree(self)).extent);
}

abi::Symbol NodeSymbol(RedNode self) {
    abi::Symbol symbol = NodeAlias(&self);
    if (symbol == 0)
        symbol = SubtreeSymbol(NodeSubtree(self));
    if (symbol == abi::kBuiltinSymbolError)
        return symbol;
    return self.tree->language->publicSymbolMap[symbol];
}

const char* NodeType(RedNode self) {
    abi::Symbol symbol = NodeAlias(&self);
    if (symbol == 0)
        symbol = SubtreeSymbol(NodeSubtree(self));
    return LanguageSymbolName(self.tree->language, symbol);
}

bool NodeIsNamed(RedNode self) {
    const abi::Symbol alias = NodeAlias(&self);
    return alias != 0 ? LanguageSymbolMetadata(self.tree->language, alias).named : SubtreeNamed(NodeSubtree(self));
}

bool NodeIsExtra(RedNode self) {
    return SubtreeExtra(NodeSubtree(self));
}

bool NodeIsMissing(RedNode self) {
    return SubtreeMissing(NodeSubtree(self));
}

bool NodeIsError(RedNode self) {
    return NodeSymbol(self) == abi::kBuiltinSymbolError;
}

bool NodeHasError(RedNode self) {
    return SubtreeErrorCost(NodeSubtree(self)) > 0;
}

bool NodeHasExternalTokens(RedNode self) {
    return SubtreeHasExternalTokens(NodeSubtree(self));
}

bool NodeEq(RedNode self, RedNode other) {
    return self.tree == other.tree && self.id == other.id;
}

const void* NodeSubtreeIdentity(RedNode self) {
    if (NodeIsNull(self)) {
        return nullptr;
    }
    const Subtree subtree = NodeSubtree(self);
    return subtree.data.isInline ? nullptr : static_cast<const void*>(subtree.ptr);
}

std::uint32_t NodeChildCount(RedNode self) {
    const Subtree tree = NodeSubtree(self);
    return SubtreeChildCount(tree) > 0 ? tree.ptr->visibleChildCount : 0;
}

std::uint32_t NodeNamedChildCount(RedNode self) {
    const Subtree tree = NodeSubtree(self);
    return SubtreeChildCount(tree) > 0 ? tree.ptr->namedChildCount : 0;
}

RedNode NodeChild(RedNode self, std::uint32_t childIndex) {
    return NodeChildImpl(self, childIndex, true);
}

RedNode NodeNamedChild(RedNode self, std::uint32_t childIndex) {
    return NodeChildImpl(self, childIndex, false);
}

RedNode NodeChildByFieldId(RedNode self, abi::FieldId fieldId) {
recur:
    if (fieldId == 0 || NodeChildCount(self) == 0)
        return NodeNull();

    const abi::FieldMapEntry* fieldMap    = nullptr;
    const abi::FieldMapEntry* fieldMapEnd = nullptr;
    LanguageFieldMap(self.tree->language, NodeSubtree(self).ptr->productionId, &fieldMap, &fieldMapEnd);
    if (fieldMap == fieldMapEnd)
        return NodeNull();

    // Field mappings are sorted by field id; narrow to this field's span.
    while (fieldMap->fieldId < fieldId) {
        fieldMap++;
        if (fieldMap == fieldMapEnd)
            return NodeNull();
    }
    while (fieldMapEnd[-1].fieldId > fieldId) {
        fieldMapEnd--;
        if (fieldMap == fieldMapEnd)
            return NodeNull();
    }

    RedNode           child;
    NodeChildIterator iterator = IterateChildren(&self);
    while (ChildIteratorNext(&iterator, &child)) {
        if (!SubtreeExtra(NodeSubtree(child))) {
            const std::uint32_t index = iterator.structuralChildIndex - 1;
            if (index < fieldMap->childIndex)
                continue;

            // Hidden nodes' fields are "inherited" by their visible parent.
            if (fieldMap->inherited) {
                // Tail-call for the last possible child for this field.
                if (fieldMap + 1 == fieldMapEnd) {
                    self = child;
                    goto recur;
                }
                const RedNode result = NodeChildByFieldId(child, fieldId);
                if (result.id != nullptr)
                    return result;
                fieldMap++;
                if (fieldMap == fieldMapEnd)
                    return NodeNull();
            }

            else if (NodeIsRelevant(child, true)) {
                return child;
            }

            // A hidden node with visible children: its first visible child.
            else if (NodeChildCount(child) > 0) {
                return NodeChild(child, 0);
            }

            else {
                fieldMap++;
                if (fieldMap == fieldMapEnd)
                    return NodeNull();
            }
        }
    }

    return NodeNull();
}

RedNode NodeChildByFieldName(RedNode self, const char* name, std::uint32_t nameLength) {
    return NodeChildByFieldId(self, LanguageFieldIdForName(self.tree->language, name, nameLength));
}

RedNode NodeParent(RedNode self) {
    // ts_tree_root_node: the root sits at its own subtree's padding.
    RedNode node = NodeNew(self.tree, &self.tree->root, SubtreePadding(self.tree->root), 0);
    if (node.id == self.id)
        return NodeNull();

    while (true) {
        RedNode nextNode = NodeChildWithDescendant(node, self);
        if (nextNode.id == self.id || NodeIsNull(nextNode))
            break;
        node = nextNode;
    }

    return node;
}

RedNode NodeChildWithDescendant(RedNode self, RedNode descendant) {
    const std::uint32_t startByte = NodeStartByte(descendant);
    const std::uint32_t endByte   = NodeEndByte(descendant);
    const bool          isEmpty   = startByte == endByte;

    do {
        NodeChildIterator iter = IterateChildren(&self);
        do {
            if (!ChildIteratorNext(&iter, &self) || NodeStartByte(self) > startByte)
                return NodeNull();
            if (self.id == descendant.id)
                return self;

            // If the descendant is empty and the end byte is within `self`,
            // check whether `self` contains it or not.
            if (isEmpty && iter.position.bytes >= endByte && NodeChildCount(self) > 0) {
                const RedNode child = NodeChildWithDescendant(self, descendant);
                if (!NodeIsNull(child))
                    return NodeIsRelevant(self, true) ? self : child;
            }
        }
        while ((isEmpty ? iter.position.bytes <= endByte : iter.position.bytes < endByte) || NodeChildCount(self) == 0);
    }
    while (!NodeIsRelevant(self, true));

    return self;
}

RedNode NodeNextSibling(RedNode self) {
    return NodeNextSiblingImpl(self, true);
}
RedNode NodeNextNamedSibling(RedNode self) {
    return NodeNextSiblingImpl(self, false);
}
RedNode NodePrevSibling(RedNode self) {
    return NodePrevSiblingImpl(self, true);
}
RedNode NodePrevNamedSibling(RedNode self) {
    return NodePrevSiblingImpl(self, false);
}

RedNode NodeDescendantForByteRange(RedNode self, std::uint32_t start, std::uint32_t end) {
    return NodeDescendantForByteRangeImpl(self, start, end, true);
}

RedNode NodeNamedDescendantForByteRange(RedNode self, std::uint32_t start, std::uint32_t end) {
    return NodeDescendantForByteRangeImpl(self, start, end, false);
}

} // namespace ned::editor::parse
