#include "Editor/Parse/Stack.h"

#include <cstdlib>

#include "Editor/Parse/LanguageTables.h"

namespace ned::editor::parse {

namespace {

    constexpr unsigned kMaxNodePoolSize  = 50;
    constexpr unsigned kMaxIteratorCount = 64;

    std::uint32_t SubtreeNodeCountForStack(Subtree subtree) {
        std::uint32_t count = SubtreeVisibleDescendantCount(subtree);
        if (SubtreeVisible(subtree))
            count++;
        // Intermediate error nodes count even though invisible: node count is how
        // progress-since-error is measured.
        if (SubtreeSymbol(subtree) == abi::kBuiltinSymbolErrorRepeat)
            count++;
        return count;
    }

    bool SubtreeIsEquivalentForStack(Subtree left, Subtree right) {
        if (left.ptr == right.ptr)
            return true;
        if (left.ptr == nullptr || right.ptr == nullptr)
            return false;

        if (SubtreeSymbol(left) != SubtreeSymbol(right))
            return false;

        // If both have errors, don't bother keeping both.
        if (SubtreeErrorCost(left) > 0 && SubtreeErrorCost(right) > 0)
            return true;

        return SubtreePadding(left).bytes == SubtreePadding(right).bytes &&
               SubtreeSize(left).bytes == SubtreeSize(right).bytes && SubtreeChildCount(left) == SubtreeChildCount(right) &&
               SubtreeExtra(left) == SubtreeExtra(right) && SubtreeExternalScannerStateEq(left, right);
    }

    void StackNodeRetain(StackNode* self) {
        if (self == nullptr)
            return;
        self->refCount++;
    }

    void StackNodeRelease(StackNode* self, RawArray<StackNode*>* pool, SubtreePool* subtreePool) {
    recur:
        self->refCount--;
        if (self->refCount > 0)
            return;

        StackNode* firstPredecessor = nullptr;
        if (self->linkCount > 0) {
            for (unsigned i = self->linkCount - 1; i > 0; i--) {
                const StackLink link = self->links[i];
                if (link.subtree.ptr != nullptr)
                    SubtreeRelease(subtreePool, link.subtree);
                StackNodeRelease(link.node, pool, subtreePool);
            }
            const StackLink link = self->links[0];
            if (link.subtree.ptr != nullptr)
                SubtreeRelease(subtreePool, link.subtree);
            firstPredecessor = self->links[0].node;
        }

        if (pool->size < kMaxNodePoolSize) {
            pool->Push(self);
        }
        else {
            std::free(self);
        }

        if (firstPredecessor != nullptr) {
            self = firstPredecessor;
            goto recur;
        }
    }

    StackNode* StackNodeNew(StackNode* previousNode, Subtree subtree, bool isPending, abi::StateId state,
                            RawArray<StackNode*>* pool) {
        StackNode* node = pool->size > 0 ? pool->Pop() : static_cast<StackNode*>(std::malloc(sizeof(StackNode)));
        *node           = StackNode{};
        node->refCount  = 1;
        node->linkCount = 0;
        node->state     = state;

        if (previousNode != nullptr) {
            node->linkCount = 1;
            node->links[0]  = StackLink{.node = previousNode, .subtree = subtree, .isPending = isPending};

            node->position          = previousNode->position;
            node->errorCost         = previousNode->errorCost;
            node->dynamicPrecedence = previousNode->dynamicPrecedence;
            node->nodeCount         = previousNode->nodeCount;

            if (subtree.ptr != nullptr) {
                node->errorCost += SubtreeErrorCost(subtree);
                node->position = LengthAdd(node->position, SubtreeTotalSize(subtree));
                node->nodeCount += SubtreeNodeCountForStack(subtree);
                node->dynamicPrecedence += SubtreeDynamicPrecedence(subtree);
            }
        }
        else {
            node->position  = LengthZero();
            node->errorCost = 0;
        }

        return node;
    }

    void StackNodeAddLink(StackNode* self, StackLink link, SubtreePool* subtreePool) {
        if (link.node == self)
            return;

        for (int i = 0; i < self->linkCount; i++) {
            StackLink* existingLink = &self->links[i];
            if (SubtreeIsEquivalentForStack(existingLink->subtree, link.subtree)) {
                // Two links directly connecting the same pair of nodes can be
                // resolved ahead of time without changing behavior.
                if (existingLink->node == link.node) {
                    if (SubtreeDynamicPrecedence(link.subtree) > SubtreeDynamicPrecedence(existingLink->subtree)) {
                        SubtreeRetain(link.subtree);
                        SubtreeRelease(subtreePool, existingLink->subtree);
                        existingLink->subtree   = link.subtree;
                        self->dynamicPrecedence = link.node->dynamicPrecedence + SubtreeDynamicPrecedence(link.subtree);
                    }
                    return;
                }

                // If the previous nodes are mergeable, merge them recursively.
                if (existingLink->node->state == link.node->state &&
                    existingLink->node->position.bytes == link.node->position.bytes &&
                    existingLink->node->errorCost == link.node->errorCost) {
                    for (int j = 0; j < link.node->linkCount; j++)
                        StackNodeAddLink(existingLink->node, link.node->links[j], subtreePool);
                    std::int32_t dynamicPrecedence = link.node->dynamicPrecedence;
                    if (link.subtree.ptr != nullptr)
                        dynamicPrecedence += SubtreeDynamicPrecedence(link.subtree);
                    if (dynamicPrecedence > self->dynamicPrecedence)
                        self->dynamicPrecedence = dynamicPrecedence;
                    return;
                }
            }
        }

        if (self->linkCount == kMaxLinkCount)
            return;

        StackNodeRetain(link.node);
        unsigned nodeCount             = link.node->nodeCount;
        int      dynamicPrecedence     = link.node->dynamicPrecedence;
        self->links[self->linkCount++] = link;

        if (link.subtree.ptr != nullptr) {
            SubtreeRetain(link.subtree);
            nodeCount += SubtreeNodeCountForStack(link.subtree);
            dynamicPrecedence += SubtreeDynamicPrecedence(link.subtree);
        }

        if (nodeCount > self->nodeCount)
            self->nodeCount = nodeCount;
        if (dynamicPrecedence > self->dynamicPrecedence)
            self->dynamicPrecedence = dynamicPrecedence;
    }

    void StackHeadDelete(StackHead* self, RawArray<StackNode*>* pool, SubtreePool* subtreePool) {
        if (self->node != nullptr) {
            if (self->lastExternalToken.ptr != nullptr)
                SubtreeRelease(subtreePool, self->lastExternalToken);
            if (self->lookaheadWhenPaused.ptr != nullptr)
                SubtreeRelease(subtreePool, self->lookaheadWhenPaused);
            if (self->summary != nullptr) {
                self->summary->Delete();
                std::free(self->summary);
            }
            StackNodeRelease(self->node, pool, subtreePool);
        }
    }

} // namespace

Stack::Stack(SubtreePool* subtreePool) : baseNode_(nullptr), subtreePool_(subtreePool) {
    heads_.Reserve(4);
    slices_.Reserve(4);
    iterators_.Reserve(4);
    nodePool_.Reserve(kMaxNodePoolSize);
    baseNode_ = StackNodeNew(nullptr, kNullSubtree, false, 1, &nodePool_);
    Clear();
}

Stack::~Stack() {
    slices_.Delete();
    iterators_.Delete();
    StackNodeRelease(baseNode_, &nodePool_, subtreePool_);
    for (std::uint32_t i = 0; i < heads_.size; i++)
        StackHeadDelete(&heads_[i], &nodePool_, subtreePool_);
    heads_.Delete();
    for (std::uint32_t i = 0; i < nodePool_.size; i++)
        std::free(nodePool_[i]);
    nodePool_.Delete();
}

std::uint32_t Stack::HaltedVersionCount() {
    std::uint32_t count = 0;
    for (std::uint32_t i = 0; i < heads_.size; i++) {
        if (heads_[i].status == StackStatus::Halted)
            count++;
    }
    return count;
}

void Stack::SetLastExternalToken(StackVersion version, Subtree token) {
    StackHead* head = &heads_[version];
    if (token.ptr != nullptr)
        SubtreeRetain(token);
    if (head->lastExternalToken.ptr != nullptr)
        SubtreeRelease(subtreePool_, head->lastExternalToken);
    head->lastExternalToken = token;
}

unsigned Stack::ErrorCost(StackVersion version) const {
    const StackHead& head   = heads_[version];
    unsigned         result = head.node->errorCost;
    if (head.status == StackStatus::Paused || (head.node->state == kErrorState && head.node->links[0].subtree.ptr == nullptr))
        result += kErrorCostPerRecovery;
    return result;
}

unsigned Stack::NodeCountSinceError(StackVersion version) {
    StackHead* head = &heads_[version];
    if (head->node->nodeCount < head->nodeCountAtLastError)
        head->nodeCountAtLastError = head->node->nodeCount;
    return head->node->nodeCount - head->nodeCountAtLastError;
}

void Stack::Push(StackVersion version, Subtree subtree, bool pending, abi::StateId state) {
    StackHead* head    = &heads_[version];
    StackNode* newNode = StackNodeNew(head->node, subtree, pending, state, &nodePool_);
    if (subtree.ptr == nullptr)
        head->nodeCountAtLastError = newNode->nodeCount;
    head->node = newNode;
}

StackVersion Stack::AddVersion(StackVersion originalVersion, StackNode* node) {
    const StackHead head = {
        .node                 = node,
        .summary              = nullptr,
        .nodeCountAtLastError = heads_[originalVersion].nodeCountAtLastError,
        .lastExternalToken    = heads_[originalVersion].lastExternalToken,
        .lookaheadWhenPaused  = kNullSubtree,
        .status               = StackStatus::Active,
    };
    heads_.Push(head);
    StackNodeRetain(node);
    if (head.lastExternalToken.ptr != nullptr)
        SubtreeRetain(head.lastExternalToken);
    return heads_.size - 1;
}

void Stack::AddSlice(StackVersion originalVersion, StackNode* node, SubtreeArray* subtrees) {
    for (std::uint32_t i = slices_.size - 1; i + 1 > 0; i--) {
        const StackVersion version = slices_[i].version;
        if (heads_[version].node == node) {
            const StackSlice slice = {*subtrees, version};
            slices_.Insert(i + 1, slice);
            return;
        }
    }

    const StackVersion version = AddVersion(originalVersion, node);
    const StackSlice   slice   = {*subtrees, version};
    slices_.Push(slice);
}

StackSliceArray Stack::Iterate(StackVersion version, Callback callback, void* payload, int goalSubtreeCount) {
    slices_.Clear();
    iterators_.Clear();

    StackHead*    head        = &heads_[version];
    StackIterator newIterator = {
        .node         = head->node,
        .subtrees     = SubtreeArray{},
        .subtreeCount = 0,
        .isPending    = true,
    };

    bool includeSubtrees = false;
    if (goalSubtreeCount >= 0) {
        includeSubtrees = true;
        newIterator.subtrees.Reserve(static_cast<std::uint32_t>(SubtreeAllocSize(goalSubtreeCount) / sizeof(Subtree)));
    }

    iterators_.Push(newIterator);

    while (iterators_.size > 0) {
        for (std::uint32_t i = 0, size = iterators_.size; i < size; i++) {
            StackIterator* iterator = &iterators_[i];
            StackNode*     node     = iterator->node;

            const StackAction action     = callback(payload, iterator);
            const bool        shouldPop  = (action & kStackActionPop) != 0;
            const bool        shouldStop = (action & kStackActionStop) != 0 || node->linkCount == 0;

            if (shouldPop) {
                SubtreeArray subtrees = iterator->subtrees;
                if (!shouldStop)
                    SubtreeArrayCopy(subtrees, &subtrees);
                SubtreeArrayReverse(&subtrees);
                AddSlice(version, node, &subtrees);
            }

            if (shouldStop) {
                if (!shouldPop)
                    SubtreeArrayDelete(subtreePool_, &iterator->subtrees);
                iterators_.Erase(i);
                i--, size--;
                continue;
            }

            for (std::uint32_t j = 1; j <= node->linkCount; j++) {
                StackIterator* nextIterator = nullptr;
                StackLink      link;
                if (j == node->linkCount) {
                    link         = node->links[0];
                    nextIterator = &iterators_[i];
                }
                else {
                    if (iterators_.size >= kMaxIteratorCount)
                        continue;
                    link                                = node->links[j];
                    const StackIterator currentIterator = iterators_[i];
                    iterators_.Push(currentIterator);
                    nextIterator = &iterators_.Back();
                    SubtreeArrayCopy(nextIterator->subtrees, &nextIterator->subtrees);
                }

                nextIterator->node = link.node;
                if (link.subtree.ptr != nullptr) {
                    if (includeSubtrees) {
                        nextIterator->subtrees.Push(link.subtree);
                        SubtreeRetain(link.subtree);
                    }
                    if (!SubtreeExtra(link.subtree)) {
                        nextIterator->subtreeCount++;
                        if (!link.isPending)
                            nextIterator->isPending = false;
                    }
                }
                else {
                    nextIterator->subtreeCount++;
                    nextIterator->isPending = false;
                }
            }
        }
    }

    return slices_;
}

StackSliceArray Stack::PopCount(StackVersion version, std::uint32_t count) {
    return Iterate(
        version,
        [](void* payload, const StackIterator* iterator) -> StackAction {
            const auto* goalSubtreeCount = static_cast<const unsigned*>(payload);
            if (iterator->subtreeCount == *goalSubtreeCount)
                return kStackActionPop | kStackActionStop;
            return kStackActionNone;
        },
        &count, static_cast<int>(count));
}

StackSliceArray Stack::PopPending(StackVersion version) {
    StackSliceArray pop = Iterate(
        version,
        [](void* payload, const StackIterator* iterator) -> StackAction {
            (void)payload;
            if (iterator->subtreeCount >= 1) {
                if (iterator->isPending)
                    return kStackActionPop | kStackActionStop;
                return kStackActionStop;
            }
            return kStackActionNone;
        },
        nullptr, 0);
    if (pop.size > 0) {
        RenumberVersion(pop[0].version, version);
        pop[0].version = version;
    }
    return pop;
}

SubtreeArray Stack::PopError(StackVersion version) {
    StackNode* node = heads_[version].node;
    for (unsigned i = 0; i < node->linkCount; i++) {
        if (node->links[i].subtree.ptr != nullptr && SubtreeIsError(node->links[i].subtree)) {
            bool            foundError = false;
            StackSliceArray pop        = Iterate(
                version,
                [](void* payload, const StackIterator* iterator) -> StackAction {
                    if (iterator->subtrees.size > 0) {
                        auto* found = static_cast<bool*>(payload);
                        if (!*found && SubtreeIsError(iterator->subtrees.contents[0])) {
                            *found = true;
                            return kStackActionPop | kStackActionStop;
                        }
                        return kStackActionStop;
                    }
                    return kStackActionNone;
                },
                &foundError, 1);
            if (pop.size > 0) {
                RenumberVersion(pop[0].version, version);
                return pop[0].subtrees;
            }
            break;
        }
    }
    return SubtreeArray{};
}

StackSliceArray Stack::PopAll(StackVersion version) {
    return Iterate(
        version,
        [](void* payload, const StackIterator* iterator) -> StackAction {
            (void)payload;
            return iterator->node->linkCount == 0 ? kStackActionPop : kStackActionNone;
        },
        nullptr, 0);
}

namespace {
    struct SummarizeSession {
        StackSummary* summary;
        unsigned      maxDepth;
    };
} // namespace

void Stack::RecordSummary(StackVersion version, unsigned maxDepth) {
    SummarizeSession session = {static_cast<StackSummary*>(std::malloc(sizeof(StackSummary))), maxDepth};
    *session.summary         = StackSummary{};
    Iterate(
        version,
        [](void* payload, const StackIterator* iterator) -> StackAction {
            auto*              sessionPtr = static_cast<SummarizeSession*>(payload);
            const abi::StateId state      = iterator->node->state;
            const unsigned     depth      = iterator->subtreeCount;
            if (depth > sessionPtr->maxDepth)
                return kStackActionStop;
            for (unsigned i = sessionPtr->summary->size - 1; i + 1 > 0; i--) {
                const StackSummaryEntry entry = (*sessionPtr->summary)[i];
                if (entry.depth < depth)
                    break;
                if (entry.depth == depth && entry.state == state)
                    return kStackActionNone;
            }
            sessionPtr->summary->Push(StackSummaryEntry{
                .position = iterator->node->position,
                .depth    = depth,
                .state    = state,
            });
            return kStackActionNone;
        },
        &session, -1);
    StackHead* head = &heads_[version];
    if (head->summary != nullptr) {
        head->summary->Delete();
        std::free(head->summary);
    }
    head->summary = session.summary;
}

bool Stack::HasAdvancedSinceError(StackVersion version) const {
    const StackHead& head = heads_[version];
    const StackNode* node = head.node;
    if (node->errorCost == 0)
        return true;
    while (node != nullptr) {
        if (node->linkCount > 0) {
            const Subtree subtree = node->links[0].subtree;
            if (subtree.ptr != nullptr) {
                if (SubtreeTotalBytes(subtree) > 0)
                    return true;
                if (node->nodeCount > head.nodeCountAtLastError && SubtreeErrorCost(subtree) == 0) {
                    node = node->links[0].node;
                    continue;
                }
            }
        }
        break;
    }
    return false;
}

void Stack::RemoveVersion(StackVersion version) {
    StackHeadDelete(&heads_[version], &nodePool_, subtreePool_);
    heads_.Erase(version);
}

void Stack::RenumberVersion(StackVersion v1, StackVersion v2) {
    if (v1 == v2)
        return;
    StackHead* sourceHead = &heads_[v1];
    StackHead* targetHead = &heads_[v2];
    if (targetHead->summary != nullptr && sourceHead->summary == nullptr) {
        sourceHead->summary = targetHead->summary;
        targetHead->summary = nullptr;
    }
    StackHeadDelete(targetHead, &nodePool_, subtreePool_);
    *targetHead = *sourceHead;
    heads_.Erase(v1);
}

void Stack::SwapVersions(StackVersion v1, StackVersion v2) {
    const StackHead temporaryHead = heads_[v1];
    heads_[v1]                    = heads_[v2];
    heads_[v2]                    = temporaryHead;
}

StackVersion Stack::CopyVersion(StackVersion version) {
    heads_.Push(heads_[version]);
    StackHead* head = &heads_.Back();
    StackNodeRetain(head->node);
    if (head->lastExternalToken.ptr != nullptr)
        SubtreeRetain(head->lastExternalToken);
    head->summary = nullptr;
    return heads_.size - 1;
}

bool Stack::Merge(StackVersion version1, StackVersion version2) {
    if (!CanMerge(version1, version2))
        return false;
    StackHead* head1 = &heads_[version1];
    StackHead* head2 = &heads_[version2];
    for (std::uint32_t i = 0; i < head2->node->linkCount; i++)
        StackNodeAddLink(head1->node, head2->node->links[i], subtreePool_);
    if (head1->node->state == kErrorState)
        head1->nodeCountAtLastError = head1->node->nodeCount;
    RemoveVersion(version2);
    return true;
}

bool Stack::CanMerge(StackVersion version1, StackVersion version2) const {
    const StackHead& head1 = heads_[version1];
    const StackHead& head2 = heads_[version2];
    return head1.status == StackStatus::Active && head2.status == StackStatus::Active &&
           head1.node->state == head2.node->state && head1.node->position.bytes == head2.node->position.bytes &&
           head1.node->errorCost == head2.node->errorCost &&
           SubtreeExternalScannerStateEq(head1.lastExternalToken, head2.lastExternalToken);
}

void Stack::Pause(StackVersion version, Subtree lookahead) {
    StackHead* head            = &heads_[version];
    head->status               = StackStatus::Paused;
    head->lookaheadWhenPaused  = lookahead;
    head->nodeCountAtLastError = head->node->nodeCount;
}

Subtree Stack::Resume(StackVersion version) {
    StackHead*    head        = &heads_[version];
    const Subtree result      = head->lookaheadWhenPaused;
    head->status              = StackStatus::Active;
    head->lookaheadWhenPaused = kNullSubtree;
    return result;
}

void Stack::Clear() {
    StackNodeRetain(baseNode_);
    for (std::uint32_t i = 0; i < heads_.size; i++)
        StackHeadDelete(&heads_[i], &nodePool_, subtreePool_);
    heads_.Clear();
    heads_.Push(StackHead{
        .node                 = baseNode_,
        .summary              = nullptr,
        .nodeCountAtLastError = 0,
        .lastExternalToken    = kNullSubtree,
        .lookaheadWhenPaused  = kNullSubtree,
        .status               = StackStatus::Active,
    });
}

} // namespace ned::editor::parse
