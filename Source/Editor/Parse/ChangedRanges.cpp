#include "Editor/Parse/ChangedRanges.h"

#include "Editor/Parse/Cursor.h"
#include "Editor/Parse/LanguageTables.h"

namespace ned::editor::parse {

namespace {

    void AddRange(std::vector<ChangedRange>& ranges, Length start, Length end) {
        if (!ranges.empty() && start.bytes <= ranges.back().end.bytes) {
            ranges.back().end = end;
            return;
        }
        if (start.bytes < end.bytes)
            ranges.push_back({start, end});
    }

    Length LengthMin(Length a, Length b) {
        return a.bytes <= b.bytes ? a : b;
    }

    abi::Symbol AliasAt(const abi::LanguageData* language, const Subtree& parent, std::uint32_t structuralChildIndex) {
        const abi::Symbol* sequence = LanguageAliasSequence(language, SubtreeProductionId(parent));
        return sequence != nullptr ? sequence[structuralChildIndex] : 0;
    }

    // A walk over one tree that stops at every visible node, and additionally
    // in the padding before a visible node, so the two trees' walks can be
    // held in lockstep by byte position and visible depth.
    class Iterator {
      public:
        Iterator(const Subtree* root, const abi::LanguageData* language) : language_(language) {
            stack_.Push(TreeCursorEntry{.subtree = root, .position = LengthZero(), .childIndex = 0, .structuralChildIndex = 0});
        }

        [[nodiscard]] bool Done() const {
            return stack_.size == 0;
        }

        [[nodiscard]] Length StartPosition() const {
            const TreeCursorEntry& entry = stack_.Back();
            return inPadding_ ? entry.position : LengthAdd(entry.position, SubtreePadding(*entry.subtree));
        }

        [[nodiscard]] Length EndPosition() const {
            const TreeCursorEntry& entry  = stack_.Back();
            const Length           result = LengthAdd(entry.position, SubtreePadding(*entry.subtree));
            return inPadding_ ? result : LengthAdd(result, SubtreeSize(*entry.subtree));
        }

        [[nodiscard]] unsigned VisibleDepth() const {
            return visibleDepth_;
        }

        [[nodiscard]] Subtree PrevExternalToken() const {
            return prevExternalToken_;
        }

        // The innermost visible node at the current position (skipping the
        // node the padding belongs to while in padding), with its alias.
        void VisibleState(Subtree* tree, abi::Symbol* aliasSymbol, std::uint32_t* startByte) const {
            std::uint32_t i = stack_.size - 1;
            if (inPadding_) {
                if (i == 0)
                    return;
                i--;
            }
            for (; i + 1 > 0; i--) {
                const TreeCursorEntry& entry = stack_[i];
                if (i > 0)
                    *aliasSymbol = AliasAt(language_, *stack_[i - 1].subtree, entry.structuralChildIndex);
                if (SubtreeVisible(*entry.subtree) || *aliasSymbol != 0) {
                    *tree      = *entry.subtree;
                    *startByte = entry.position.bytes;
                    break;
                }
            }
        }

        void Ascend() {
            if (Done())
                return;
            if (TreeIsVisible() && !inPadding_)
                visibleDepth_--;
            if (stack_.Back().childIndex > 0)
                inPadding_ = false;
            stack_.size--;
        }

        bool Descend(std::uint32_t goalPosition) {
            if (inPadding_)
                return false;

            bool didDescend = false;
            do {
                didDescend                         = false;
                const TreeCursorEntry entry        = stack_.Back();
                Length                position     = entry.position;
                std::uint32_t         structuralIx = 0;
                for (std::uint32_t i = 0, n = SubtreeChildCount(*entry.subtree); i < n; i++) {
                    const Subtree* child      = &SubtreeChildren(*entry.subtree)[i];
                    const Length   childLeft  = LengthAdd(position, SubtreePadding(*child));
                    const Length   childRight = LengthAdd(childLeft, SubtreeSize(*child));

                    if (childRight.bytes > goalPosition) {
                        stack_.Push(TreeCursorEntry{.subtree = child, .position = position, .childIndex = i, .structuralChildIndex = structuralIx});
                        if (TreeIsVisible()) {
                            if (childLeft.bytes > goalPosition)
                                inPadding_ = true;
                            else
                                visibleDepth_++;
                            return true;
                        }
                        didDescend = true;
                        break;
                    }

                    position = childRight;
                    if (!SubtreeExtra(*child))
                        structuralIx++;
                    NoteExternalToken(*child);
                }
            }
            while (didDescend);

            return false;
        }

        void Advance() {
            if (inPadding_) {
                inPadding_ = false;
                if (TreeIsVisible())
                    visibleDepth_++;
                else
                    Descend(0);
                return;
            }

            for (;;) {
                if (TreeIsVisible())
                    visibleDepth_--;
                const TreeCursorEntry entry = stack_.Pop();
                if (Done())
                    return;

                const Subtree*      parent     = stack_.Back().subtree;
                const std::uint32_t childIndex = entry.childIndex + 1;
                NoteExternalToken(*entry.subtree);
                if (SubtreeChildCount(*parent) > childIndex) {
                    const Length  position     = LengthAdd(entry.position, SubtreeTotalSize(*entry.subtree));
                    std::uint32_t structuralIx = entry.structuralChildIndex;
                    if (!SubtreeExtra(*entry.subtree))
                        structuralIx++;
                    const Subtree* nextChild = &SubtreeChildren(*parent)[childIndex];

                    stack_.Push(TreeCursorEntry{.subtree = nextChild, .position = position, .childIndex = childIndex, .structuralChildIndex = structuralIx});
                    if (TreeIsVisible()) {
                        if (SubtreePadding(*nextChild).bytes > 0)
                            inPadding_ = true;
                        else
                            visibleDepth_++;
                    }
                    else {
                        Descend(0);
                    }
                    break;
                }
            }
        }

      private:
        [[nodiscard]] bool TreeIsVisible() const {
            const TreeCursorEntry& entry = stack_.Back();
            if (SubtreeVisible(*entry.subtree))
                return true;
            if (stack_.size > 1)
                return AliasAt(language_, *stack_[stack_.size - 2].subtree, entry.structuralChildIndex) != 0;
            return false;
        }

        void NoteExternalToken(Subtree tree) {
            const Subtree last = SubtreeLastExternalToken(tree);
            if (last.ptr != nullptr)
                prevExternalToken_ = last;
        }

        CursorStack              stack_;
        const abi::LanguageData* language_;
        unsigned                 visibleDepth_      = 1;
        bool                     inPadding_         = false;
        Subtree                  prevExternalToken_ = kNullSubtree;
    };

    enum class Comparison : std::uint8_t { Differs,
                                           MayDiffer,
                                           Matches };

    Comparison Compare(const Iterator& oldIter, const Iterator& newIter) {
        Subtree       oldTree  = kNullSubtree;
        Subtree       newTree  = kNullSubtree;
        std::uint32_t oldStart = 0;
        std::uint32_t newStart = 0;
        abi::Symbol   oldAlias = 0;
        abi::Symbol   newAlias = 0;
        oldIter.VisibleState(&oldTree, &oldAlias, &oldStart);
        newIter.VisibleState(&newTree, &newAlias, &newStart);

        if (oldTree.ptr == nullptr && newTree.ptr == nullptr)
            return Comparison::Matches;
        if (oldTree.ptr == nullptr || newTree.ptr == nullptr)
            return Comparison::Differs;
        const abi::Symbol oldSymbol = SubtreeSymbol(oldTree);
        const abi::Symbol newSymbol = SubtreeSymbol(newTree);
        if (oldAlias != newAlias || oldSymbol != newSymbol)
            return Comparison::Differs;

        const abi::StateId oldState       = SubtreeParseState(oldTree);
        const abi::StateId newState       = SubtreeParseState(newTree);
        const bool         oldHasExternal = SubtreeHasExternalTokens(oldTree);
        const bool         newHasExternal = SubtreeHasExternalTokens(newTree);
        if (oldStart != newStart || oldSymbol == abi::kBuiltinSymbolError ||
            SubtreeSize(oldTree).bytes != SubtreeSize(newTree).bytes || oldState == kTreeStateNone ||
            newState == kTreeStateNone || (oldState == kErrorState) != (newState == kErrorState) ||
            SubtreeErrorCost(oldTree) != SubtreeErrorCost(newTree) || oldHasExternal != newHasExternal ||
            SubtreeHasChanges(oldTree) ||
            (oldHasExternal && !SubtreeExternalScannerStateEq(oldIter.PrevExternalToken(), newIter.PrevExternalToken())))
            return Comparison::MayDiffer;

        return Comparison::Matches;
    }

} // namespace

std::vector<ChangedRange> ChangedRanges(const GreenTree& oldEdited, const GreenTree& newTree) {
    std::vector<ChangedRange> results;
    if (oldEdited.IsNull() || newTree.IsNull())
        return results;

    const Subtree* oldRoot = &oldEdited.Data()->root;
    const Subtree* newRoot = &newTree.Data()->root;
    Iterator       oldIter(oldRoot, oldEdited.Language());
    Iterator       newIter(newRoot, newTree.Language());

    Length position     = oldIter.StartPosition();
    Length nextPosition = newIter.StartPosition();
    if (position.bytes < nextPosition.bytes) {
        AddRange(results, position, nextPosition);
        position = nextPosition;
    }
    else if (position.bytes > nextPosition.bytes) {
        AddRange(results, nextPosition, position);
        nextPosition = position;
    }

    do {
        bool isChanged = false;
        switch (Compare(oldIter, newIter)) {
            // Definitely identical: skip both past the subtree.
            case Comparison::Matches:
                nextPosition = oldIter.EndPosition();
                break;

            // Possibly different inside: descend both to the first child
            // spanning the current position.
            case Comparison::MayDiffer:
                if (oldIter.Descend(position.bytes)) {
                    if (!newIter.Descend(position.bytes)) {
                        isChanged    = true;
                        nextPosition = oldIter.EndPosition();
                    }
                }
                else if (newIter.Descend(position.bytes)) {
                    isChanged    = true;
                    nextPosition = newIter.EndPosition();
                }
                else {
                    nextPosition = LengthMin(oldIter.EndPosition(), newIter.EndPosition());
                }
                break;

            case Comparison::Differs:
                isChanged    = true;
                nextPosition = LengthMin(oldIter.EndPosition(), newIter.EndPosition());
                break;
        }

        while (!oldIter.Done() && oldIter.EndPosition().bytes <= nextPosition.bytes)
            oldIter.Advance();
        while (!newIter.Done() && newIter.EndPosition().bytes <= nextPosition.bytes)
            newIter.Advance();

        while (oldIter.VisibleDepth() > newIter.VisibleDepth())
            oldIter.Ascend();
        while (newIter.VisibleDepth() > oldIter.VisibleDepth())
            newIter.Ascend();

        if (isChanged)
            AddRange(results, position, nextPosition);

        position = nextPosition;
    }
    while (!oldIter.Done() && !newIter.Done());

    const Length oldSize = SubtreeTotalSize(*oldRoot);
    const Length newSize = SubtreeTotalSize(*newRoot);
    if (oldSize.bytes < newSize.bytes)
        AddRange(results, oldSize, newSize);
    else if (newSize.bytes < oldSize.bytes)
        AddRange(results, newSize, oldSize);

    return results;
}

} // namespace ned::editor::parse
