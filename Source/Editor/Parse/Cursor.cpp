#include "Editor/Parse/Cursor.h"

#include "Editor/Parse/LanguageTables.h"

namespace ned::editor::parse {

namespace {

    struct CursorChildIterator {
        Subtree            parent;
        const TreeData*    tree;
        Length             position;
        std::uint32_t      childIndex;
        std::uint32_t      structuralChildIndex;
        const abi::Symbol* aliasSequence;
    };

    abi::Symbol AliasAt(const abi::LanguageData* language, std::uint32_t productionId, std::uint32_t childIndex) {
        return productionId != 0 ? language->aliasSequences[productionId * language->maxAliasSequenceLength + childIndex] : 0;
    }

    bool ChildIteratorNext(CursorChildIterator* self, TreeCursorEntry* result, bool* visible) {
        if (self->parent.ptr == nullptr || self->childIndex == self->parent.ptr->childCount)
            return false;
        const Subtree* child = &SubtreeChildren(self->parent)[self->childIndex];
        *result              = TreeCursorEntry{
            .subtree              = child,
            .position             = self->position,
            .childIndex           = self->childIndex,
            .structuralChildIndex = self->structuralChildIndex,
        };
        *visible         = SubtreeVisible(*child);
        const bool extra = SubtreeExtra(*child);
        if (!extra) {
            if (self->aliasSequence != nullptr)
                *visible |= self->aliasSequence[self->structuralChildIndex] != 0;
            self->structuralChildIndex++;
        }

        self->position = LengthAdd(self->position, SubtreeSize(*child));
        self->childIndex++;

        if (self->childIndex < self->parent.ptr->childCount) {
            const Subtree nextChild = SubtreeChildren(self->parent)[self->childIndex];
            self->position          = LengthAdd(self->position, SubtreePadding(nextChild));
        }

        return true;
    }

} // namespace

void TreeCursor::Reset(RedNode node) {
    tree_            = node.tree;
    rootAliasSymbol_ = static_cast<abi::Symbol>(node.context[3]);
    stack_.Clear();
    stack_.Push(TreeCursorEntry{
        .subtree              = node.id,
        .position             = {NodeStartByte(node), NodeStartPoint(node)},
        .childIndex           = 0,
        .structuralChildIndex = 0,
    });
}

bool TreeCursor::IsEntryVisible(std::uint32_t index) const {
    const TreeCursorEntry& entry = stack_[index];
    if (index == 0 || SubtreeVisible(*entry.subtree))
        return true;
    if (!SubtreeExtra(*entry.subtree)) {
        const TreeCursorEntry& parentEntry = stack_[index - 1];
        return AliasAt(tree_->language, parentEntry.subtree->ptr->productionId, entry.structuralChildIndex) != 0;
    }
    return false;
}

TreeCursor::Step TreeCursor::GotoFirstChildInternal() {
    const TreeCursorEntry& lastEntry = stack_.Back();
    if (SubtreeChildCount(*lastEntry.subtree) == 0)
        return Step::None;
    CursorChildIterator iterator = {
        .parent               = *lastEntry.subtree,
        .tree                 = tree_,
        .position             = lastEntry.position,
        .childIndex           = 0,
        .structuralChildIndex = 0,
        .aliasSequence        = LanguageAliasSequence(tree_->language, lastEntry.subtree->ptr->productionId),
    };

    bool            visible = false;
    TreeCursorEntry entry;
    while (ChildIteratorNext(&iterator, &entry, &visible)) {
        if (visible) {
            stack_.Push(entry);
            return Step::Visible;
        }
        if (SubtreeVisibleChildCount(*entry.subtree) > 0) {
            stack_.Push(entry);
            return Step::Hidden;
        }
    }
    return Step::None;
}

bool TreeCursor::GotoFirstChild() {
    for (;;) {
        switch (GotoFirstChildInternal()) {
            case Step::Hidden:
                continue;
            case Step::Visible:
                return true;
            default:
                return false;
        }
    }
}

TreeCursor::Step TreeCursor::GotoNextSiblingInternal() {
    const std::uint32_t initialSize = stack_.size;

    while (stack_.size > 1) {
        const TreeCursorEntry  poppedEntry = stack_.Pop();
        const TreeCursorEntry& parentEntry = stack_.Back();
        CursorChildIterator    iterator    = {
            .parent               = *parentEntry.subtree,
            .tree                 = tree_,
            .position             = poppedEntry.position,
            .childIndex           = poppedEntry.childIndex,
            .structuralChildIndex = poppedEntry.structuralChildIndex,
            .aliasSequence        = LanguageAliasSequence(tree_->language, parentEntry.subtree->ptr->productionId),
        };

        bool            visible = false;
        TreeCursorEntry entry;
        ChildIteratorNext(&iterator, &entry, &visible);
        if (visible && stack_.size + 1 < initialSize)
            break;

        while (ChildIteratorNext(&iterator, &entry, &visible)) {
            if (visible) {
                stack_.Push(entry);
                return Step::Visible;
            }
            if (SubtreeVisibleChildCount(*entry.subtree) > 0) {
                stack_.Push(entry);
                return Step::Hidden;
            }
        }
    }

    stack_.size = initialSize;
    return Step::None;
}

bool TreeCursor::GotoNextSibling() {
    switch (GotoNextSiblingInternal()) {
        case Step::Hidden:
            GotoFirstChild();
            return true;
        case Step::Visible:
            return true;
        default:
            return false;
    }
}

bool TreeCursor::GotoParent() {
    for (unsigned i = stack_.size - 2; i + 1 > 0; i--) {
        if (IsEntryVisible(i)) {
            stack_.size = i + 1;
            return true;
        }
    }
    return false;
}

RedNode TreeCursor::CurrentNode() const {
    const TreeCursorEntry& lastEntry   = stack_[stack_.size - 1];
    const bool             isExtra     = SubtreeExtra(*lastEntry.subtree);
    abi::Symbol            aliasSymbol = isExtra ? 0 : rootAliasSymbol_;
    if (stack_.size > 1 && !isExtra) {
        const TreeCursorEntry& parentEntry = stack_[stack_.size - 2];
        aliasSymbol                        = AliasAt(tree_->language, parentEntry.subtree->ptr->productionId, lastEntry.structuralChildIndex);
    }
    return NodeNew(tree_, lastEntry.subtree, lastEntry.position, aliasSymbol);
}

abi::FieldId TreeCursor::CurrentFieldId() const {
    // Walk up through the current node and its invisible ancestors: fields
    // can refer to nodes through hidden wrapper nodes.
    for (unsigned i = stack_.size - 1; i > 0; i--) {
        const TreeCursorEntry& entry       = stack_[i];
        const TreeCursorEntry& parentEntry = stack_[i - 1];

        if (i != stack_.size - 1 && IsEntryVisible(i))
            break;

        if (SubtreeExtra(*entry.subtree))
            break;

        const abi::FieldMapEntry* fieldMap    = nullptr;
        const abi::FieldMapEntry* fieldMapEnd = nullptr;
        LanguageFieldMap(tree_->language, parentEntry.subtree->ptr->productionId, &fieldMap, &fieldMapEnd);
        for (const abi::FieldMapEntry* map = fieldMap; map < fieldMapEnd; map++) {
            if (!map->inherited && map->childIndex == entry.structuralChildIndex)
                return map->fieldId;
        }
    }
    return 0;
}

} // namespace ned::editor::parse
