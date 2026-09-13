#include "Editor/Parse/Green.h"

#include <atomic>
#include <cstdlib>
#include <cstring>

#include "Editor/Parse/LanguageTables.h"

namespace ned::editor::parse {

namespace {

    constexpr std::uint32_t kMaxInlineTreeLength = 255;
    constexpr std::uint32_t kMaxTreePoolSize     = 32;

    static_assert(sizeof(SubtreeInlineData) == 8);
    static_assert(sizeof(Subtree) == 8);

    std::uint32_t AtomicInc(volatile std::uint32_t* p) {
        return std::atomic_ref<std::uint32_t>(*const_cast<std::uint32_t*>(p)).fetch_add(1, std::memory_order_relaxed) + 1;
    }

    std::uint32_t AtomicDec(volatile std::uint32_t* p) {
        return std::atomic_ref<std::uint32_t>(*const_cast<std::uint32_t*>(p)).fetch_sub(1, std::memory_order_acq_rel) - 1;
    }

    bool SubtreeCanInline(Length padding, Length size, std::uint32_t lookaheadBytes) {
        return padding.bytes < kMaxInlineTreeLength && padding.extent.row < 16 && padding.extent.column < kMaxInlineTreeLength &&
               size.bytes < kMaxInlineTreeLength && size.extent.row == 0 && size.extent.column < kMaxInlineTreeLength &&
               lookaheadBytes < 16;
    }

} // namespace

// --- ExternalScannerState ---------------------------------------------------

void ExternalScannerState::Init(const char* data, unsigned dataLength) {
    length = dataLength;
    if (dataLength > sizeof(shortData)) {
        longData = static_cast<char*>(std::malloc(dataLength));
        std::memcpy(longData, data, dataLength);
    }
    else {
        std::memcpy(shortData, data, dataLength);
    }
}

const char* ExternalScannerState::Data() const {
    if (length > sizeof(shortData))
        return longData;
    return shortData;
}

bool ExternalScannerState::Eq(const char* buffer, unsigned bufferLength) const {
    return length == bufferLength && std::memcmp(Data(), buffer, length) == 0;
}

ExternalScannerState ExternalScannerState::Copy() const {
    ExternalScannerState result = *this;
    if (length > sizeof(shortData)) {
        result.longData = static_cast<char*>(std::malloc(length));
        std::memcpy(result.longData, longData, length);
    }
    return result;
}

void ExternalScannerState::Delete() {
    if (length > sizeof(shortData))
        std::free(longData);
}

// --- SubtreeArray -----------------------------------------------------------

void SubtreeArrayCopy(SubtreeArray self, SubtreeArray* dest) {
    dest->size     = self.size;
    dest->capacity = self.capacity;
    dest->contents = self.contents;
    if (self.capacity > 0) {
        dest->contents = static_cast<Subtree*>(std::calloc(self.capacity, sizeof(Subtree)));
        std::memcpy(dest->contents, self.contents, self.size * sizeof(Subtree));
        for (std::uint32_t i = 0; i < self.size; i++)
            SubtreeRetain(dest->contents[i]);
    }
}

void SubtreeArrayClear(SubtreePool* pool, SubtreeArray* self) {
    for (std::uint32_t i = 0; i < self->size; i++)
        SubtreeRelease(pool, self->contents[i]);
    self->Clear();
}

void SubtreeArrayDelete(SubtreePool* pool, SubtreeArray* self) {
    SubtreeArrayClear(pool, self);
    self->Delete();
}

void SubtreeArrayRemoveTrailingExtras(SubtreeArray* self, SubtreeArray* destination) {
    destination->Clear();
    while (self->size > 0) {
        const Subtree last = self->contents[self->size - 1];
        if (!SubtreeExtra(last))
            break;
        self->size--;
        destination->Push(last);
    }
    SubtreeArrayReverse(destination);
}

void SubtreeArrayReverse(SubtreeArray* self) {
    for (std::uint32_t i = 0, limit = self->size / 2; i < limit; i++) {
        const std::uint32_t reverseIndex = self->size - 1 - i;
        const Subtree       swap         = self->contents[i];
        self->contents[i]                = self->contents[reverseIndex];
        self->contents[reverseIndex]     = swap;
    }
}

// --- SubtreePool ------------------------------------------------------------

SubtreePool SubtreePool::New(std::uint32_t capacity) {
    SubtreePool self{};
    self.freeTrees.Reserve(capacity);
    return self;
}

void SubtreePool::Delete() {
    if (freeTrees.contents != nullptr) {
        for (std::uint32_t i = 0; i < freeTrees.size; i++)
            std::free(freeTrees[i].ptr);
        freeTrees.Delete();
    }
    if (treeStack.contents != nullptr)
        treeStack.Delete();
}

SubtreeHeapData* SubtreePool::Allocate() {
    if (freeTrees.size > 0)
        return freeTrees.Pop().ptr;
    return static_cast<SubtreeHeapData*>(std::malloc(sizeof(SubtreeHeapData)));
}

void SubtreePool::Free(SubtreeHeapData* tree) {
    if (freeTrees.capacity > 0 && freeTrees.size + 1 <= kMaxTreePoolSize) {
        freeTrees.Push(MutableSubtree{.ptr = tree});
    }
    else {
        std::free(tree);
    }
}

// --- Subtree ----------------------------------------------------------------

Subtree SubtreeNewLeaf(SubtreePool* pool, abi::Symbol symbol, Length padding, Length size, std::uint32_t lookaheadBytes,
                       abi::StateId parseState, bool hasExternalTokens, bool dependsOnColumn, bool isKeyword,
                       const abi::LanguageData* language) {
    const abi::SymbolMetadata metadata = LanguageSymbolMetadata(language, symbol);
    const bool                extra    = symbol == abi::kBuiltinSymbolEnd;

    const bool isInline = symbol <= 255 && !hasExternalTokens && SubtreeCanInline(padding, size, lookaheadBytes);
    if (isInline) {
        Subtree result;
        result.data = SubtreeInlineData{
            .isInline       = true,
            .visible        = metadata.visible,
            .named          = metadata.named,
            .extra          = extra,
            .hasChanges     = false,
            .isMissing      = false,
            .isKeyword      = isKeyword,
            .symbol         = static_cast<std::uint8_t>(symbol),
            .parseState     = parseState,
            .paddingColumns = static_cast<std::uint8_t>(padding.extent.column),
            .paddingRows    = static_cast<std::uint8_t>(padding.extent.row & 0x0F),
            .lookaheadBytes = static_cast<std::uint8_t>(lookaheadBytes & 0x0F),
            .paddingBytes   = static_cast<std::uint8_t>(padding.bytes),
            .sizeBytes      = static_cast<std::uint8_t>(size.bytes),
        };
        return result;
    }

    SubtreeHeapData* data               = pool->Allocate();
    *data                               = SubtreeHeapData{};
    data->refCount                      = 1;
    data->padding                       = padding;
    data->size                          = size;
    data->lookaheadBytes                = lookaheadBytes;
    data->errorCost                     = 0;
    data->childCount                    = 0;
    data->symbol                        = symbol;
    data->parseState                    = parseState;
    data->visible                       = metadata.visible;
    data->named                         = metadata.named;
    data->extra                         = extra;
    data->fragileLeft                   = false;
    data->fragileRight                  = false;
    data->hasChanges                    = false;
    data->hasExternalTokens             = hasExternalTokens;
    data->hasExternalScannerStateChange = false;
    data->dependsOnColumn               = dependsOnColumn;
    data->isMissing                     = false;
    data->isKeyword                     = isKeyword;
    data->firstLeaf                     = {.symbol = 0, .parseState = 0};
    return Subtree{.ptr = data};
}

void SubtreeSetSymbol(MutableSubtree* self, abi::Symbol symbol, const abi::LanguageData* language) {
    const abi::SymbolMetadata metadata = LanguageSymbolMetadata(language, symbol);
    if (self->data.isInline) {
        self->data.symbol  = static_cast<std::uint8_t>(symbol);
        self->data.named   = metadata.named;
        self->data.visible = metadata.visible;
    }
    else {
        self->ptr->symbol  = symbol;
        self->ptr->named   = metadata.named;
        self->ptr->visible = metadata.visible;
    }
}

Subtree SubtreeNewError(SubtreePool* pool, std::int32_t lookaheadChar, Length padding, Length size,
                        std::uint32_t bytesScanned, abi::StateId parseState, const abi::LanguageData* language) {
    const Subtree result = SubtreeNewLeaf(pool, abi::kBuiltinSymbolError, padding, size, bytesScanned, parseState, false,
                                          false, false, language);
    auto*         data   = const_cast<SubtreeHeapData*>(result.ptr);
    data->fragileLeft    = true;
    data->fragileRight   = true;
    data->lookaheadChar  = lookaheadChar;
    return result;
}

MutableSubtree SubtreeClone(Subtree self) {
    const std::size_t allocSize   = SubtreeAllocSize(self.ptr->childCount);
    auto*             newChildren = static_cast<Subtree*>(std::malloc(allocSize));
    Subtree*          oldChildren = SubtreeChildren(self);
    std::memcpy(newChildren, oldChildren, allocSize);
    auto* result = reinterpret_cast<SubtreeHeapData*>(&newChildren[self.ptr->childCount]);
    if (self.ptr->childCount > 0) {
        for (std::uint32_t i = 0; i < self.ptr->childCount; i++)
            SubtreeRetain(newChildren[i]);
    }
    else if (self.ptr->hasExternalTokens) {
        result->externalScannerState = self.ptr->externalScannerState.Copy();
    }
    result->refCount = 1;
    return MutableSubtree{.ptr = result};
}

MutableSubtree SubtreeMakeMut(SubtreePool* pool, Subtree self) {
    if (self.data.isInline)
        return MutableSubtree{.data = self.data};
    if (self.ptr->refCount == 1)
        return SubtreeToMutUnsafe(self);
    const MutableSubtree result = SubtreeClone(self);
    SubtreeRelease(pool, self);
    return result;
}

void SubtreeCompress(MutableSubtree self, unsigned count, const abi::LanguageData* language, MutableSubtreeArray* stack) {
    const unsigned initialStackSize = stack->size;

    MutableSubtree    tree   = self;
    const abi::Symbol symbol = tree.ptr->symbol;
    for (unsigned i = 0; i < count; i++) {
        if (tree.ptr->refCount > 1 || tree.ptr->childCount < 2)
            break;

        MutableSubtree child = SubtreeToMutUnsafe(SubtreeChildren(tree)[0]);
        if (child.data.isInline || child.ptr->childCount < 2 || child.ptr->refCount > 1 || child.ptr->symbol != symbol)
            break;

        MutableSubtree grandchild = SubtreeToMutUnsafe(SubtreeChildren(child)[0]);
        if (grandchild.data.isInline || grandchild.ptr->childCount < 2 || grandchild.ptr->refCount > 1 ||
            grandchild.ptr->symbol != symbol)
            break;

        SubtreeChildren(tree)[0]                                    = SubtreeFromMut(grandchild);
        SubtreeChildren(child)[0]                                   = SubtreeChildren(grandchild)[grandchild.ptr->childCount - 1];
        SubtreeChildren(grandchild)[grandchild.ptr->childCount - 1] = SubtreeFromMut(child);
        stack->Push(tree);
        tree = grandchild;
    }

    while (stack->size > initialStackSize) {
        tree                      = stack->Pop();
        MutableSubtree child      = SubtreeToMutUnsafe(SubtreeChildren(tree)[0]);
        MutableSubtree grandchild = SubtreeToMutUnsafe(SubtreeChildren(child)[child.ptr->childCount - 1]);
        SubtreeSummarizeChildren(grandchild, language);
        SubtreeSummarizeChildren(child, language);
        SubtreeSummarizeChildren(tree, language);
    }
}

void SubtreeSummarizeChildren(MutableSubtree self, const abi::LanguageData* language) {
    self.ptr->namedChildCount               = 0;
    self.ptr->visibleChildCount             = 0;
    self.ptr->errorCost                     = 0;
    self.ptr->repeatDepth                   = 0;
    self.ptr->visibleDescendantCount        = 0;
    self.ptr->hasExternalTokens             = false;
    self.ptr->dependsOnColumn               = false;
    self.ptr->hasExternalScannerStateChange = false;
    self.ptr->dynamicPrecedence             = 0;

    std::uint32_t      structuralIndex  = 0;
    const abi::Symbol* aliasSequence    = LanguageAliasSequence(language, self.ptr->productionId);
    std::uint32_t      lookaheadEndByte = 0;

    const Subtree* children = SubtreeChildren(self);
    for (std::uint32_t i = 0; i < self.ptr->childCount; i++) {
        const Subtree child = children[i];

        if (self.ptr->size.extent.row == 0 && SubtreeDependsOnColumn(child))
            self.ptr->dependsOnColumn = true;

        if (SubtreeHasExternalScannerStateChange(child))
            self.ptr->hasExternalScannerStateChange = true;

        if (i == 0) {
            self.ptr->padding = SubtreePadding(child);
            self.ptr->size    = SubtreeSize(child);
        }
        else {
            self.ptr->size = LengthAdd(self.ptr->size, SubtreeTotalSize(child));
        }

        const std::uint32_t childLookaheadEndByte =
            self.ptr->padding.bytes + self.ptr->size.bytes + SubtreeLookaheadBytes(child);
        if (childLookaheadEndByte > lookaheadEndByte)
            lookaheadEndByte = childLookaheadEndByte;

        if (SubtreeSymbol(child) != abi::kBuiltinSymbolErrorRepeat)
            self.ptr->errorCost += SubtreeErrorCost(child);

        const std::uint32_t grandchildCount = SubtreeChildCount(child);
        if (self.ptr->symbol == abi::kBuiltinSymbolError || self.ptr->symbol == abi::kBuiltinSymbolErrorRepeat) {
            if (!SubtreeExtra(child) && !(SubtreeIsError(child) && grandchildCount == 0)) {
                if (SubtreeVisible(child)) {
                    self.ptr->errorCost += kErrorCostPerSkippedTree;
                }
                else if (grandchildCount > 0) {
                    self.ptr->errorCost += kErrorCostPerSkippedTree * child.ptr->visibleChildCount;
                }
            }
        }

        self.ptr->dynamicPrecedence += SubtreeDynamicPrecedence(child);
        self.ptr->visibleDescendantCount += SubtreeVisibleDescendantCount(child);

        if (!SubtreeExtra(child) && SubtreeSymbol(child) != 0 && aliasSequence != nullptr &&
            aliasSequence[structuralIndex] != 0) {
            self.ptr->visibleDescendantCount++;
            self.ptr->visibleChildCount++;
            if (LanguageSymbolMetadata(language, aliasSequence[structuralIndex]).named)
                self.ptr->namedChildCount++;
        }
        else if (SubtreeVisible(child)) {
            self.ptr->visibleDescendantCount++;
            self.ptr->visibleChildCount++;
            if (SubtreeNamed(child))
                self.ptr->namedChildCount++;
        }
        else if (grandchildCount > 0) {
            self.ptr->visibleChildCount += child.ptr->visibleChildCount;
            self.ptr->namedChildCount += child.ptr->namedChildCount;
        }

        if (SubtreeHasExternalTokens(child))
            self.ptr->hasExternalTokens = true;

        if (SubtreeIsError(child)) {
            self.ptr->fragileLeft = self.ptr->fragileRight = true;
            self.ptr->parseState                           = kTreeStateNone;
        }

        if (!SubtreeExtra(child))
            structuralIndex++;
    }

    self.ptr->lookaheadBytes = lookaheadEndByte - self.ptr->size.bytes - self.ptr->padding.bytes;

    if (self.ptr->symbol == abi::kBuiltinSymbolError || self.ptr->symbol == abi::kBuiltinSymbolErrorRepeat) {
        self.ptr->errorCost += kErrorCostPerRecovery + kErrorCostPerSkippedChar * self.ptr->size.bytes +
                               kErrorCostPerSkippedLine * self.ptr->size.extent.row;
    }

    if (self.ptr->childCount > 0) {
        const Subtree firstChild = children[0];
        const Subtree lastChild  = children[self.ptr->childCount - 1];

        self.ptr->firstLeaf.symbol     = SubtreeLeafSymbol(firstChild);
        self.ptr->firstLeaf.parseState = SubtreeLeafParseState(firstChild);

        if (SubtreeFragileLeft(firstChild))
            self.ptr->fragileLeft = true;
        if (SubtreeFragileRight(lastChild))
            self.ptr->fragileRight = true;

        if (self.ptr->childCount >= 2 && !self.ptr->visible && !self.ptr->named &&
            SubtreeSymbol(firstChild) == self.ptr->symbol) {
            if (SubtreeRepeatDepth(firstChild) > SubtreeRepeatDepth(lastChild)) {
                self.ptr->repeatDepth = static_cast<std::uint16_t>(SubtreeRepeatDepth(firstChild) + 1);
            }
            else {
                self.ptr->repeatDepth = static_cast<std::uint16_t>(SubtreeRepeatDepth(lastChild) + 1);
            }
        }
    }
}

MutableSubtree SubtreeNewNode(abi::Symbol symbol, SubtreeArray* children, unsigned productionId,
                              const abi::LanguageData* language) {
    const abi::SymbolMetadata metadata = LanguageSymbolMetadata(language, symbol);
    const bool                fragile  = symbol == abi::kBuiltinSymbolError || symbol == abi::kBuiltinSymbolErrorRepeat;

    // Allocate the node's data at the end of the array of children.
    const std::size_t newByteSize = SubtreeAllocSize(children->size);
    if (children->capacity * sizeof(Subtree) < newByteSize) {
        children->contents = static_cast<Subtree*>(std::realloc(children->contents, newByteSize));
        children->capacity = static_cast<std::uint32_t>(newByteSize / sizeof(Subtree));
    }
    auto* data = reinterpret_cast<SubtreeHeapData*>(&children->contents[children->size]);

    *data                               = SubtreeHeapData{};
    data->refCount                      = 1;
    data->symbol                        = symbol;
    data->childCount                    = children->size;
    data->visible                       = metadata.visible;
    data->named                         = metadata.named;
    data->hasChanges                    = false;
    data->hasExternalScannerStateChange = false;
    data->fragileLeft                   = fragile;
    data->fragileRight                  = fragile;
    data->isKeyword                     = false;
    data->visibleDescendantCount        = 0;
    data->productionId                  = static_cast<std::uint16_t>(productionId);
    data->firstLeaf                     = {.symbol = 0, .parseState = 0};

    MutableSubtree result{.ptr = data};
    SubtreeSummarizeChildren(result, language);
    return result;
}

Subtree SubtreeNewErrorNode(SubtreeArray* children, bool extra, const abi::LanguageData* language) {
    const MutableSubtree result = SubtreeNewNode(abi::kBuiltinSymbolError, children, 0, language);
    result.ptr->extra           = extra;
    return SubtreeFromMut(result);
}

Subtree SubtreeNewMissingLeaf(SubtreePool* pool, abi::Symbol symbol, Length padding, std::uint32_t lookaheadBytes,
                              const abi::LanguageData* language) {
    Subtree result = SubtreeNewLeaf(pool, symbol, padding, LengthZero(), lookaheadBytes, 0, false, false, false, language);
    if (result.data.isInline) {
        result.data.isMissing = true;
    }
    else {
        const_cast<SubtreeHeapData*>(result.ptr)->isMissing = true;
    }
    return result;
}

void SubtreeRetain(Subtree self) {
    if (self.data.isInline)
        return;
    AtomicInc(&const_cast<SubtreeHeapData*>(self.ptr)->refCount);
}

void SubtreeRelease(SubtreePool* pool, Subtree self) {
    if (self.data.isInline)
        return;
    pool->treeStack.Clear();

    if (AtomicDec(&const_cast<SubtreeHeapData*>(self.ptr)->refCount) == 0)
        pool->treeStack.Push(SubtreeToMutUnsafe(self));

    while (pool->treeStack.size > 0) {
        const MutableSubtree tree = pool->treeStack.Pop();
        if (tree.ptr->childCount > 0) {
            Subtree* children = SubtreeChildren(tree);
            for (std::uint32_t i = 0; i < tree.ptr->childCount; i++) {
                const Subtree child = children[i];
                if (child.data.isInline)
                    continue;
                if (AtomicDec(&const_cast<SubtreeHeapData*>(child.ptr)->refCount) == 0)
                    pool->treeStack.Push(SubtreeToMutUnsafe(child));
            }
            std::free(children);
        }
        else {
            if (tree.ptr->hasExternalTokens)
                tree.ptr->externalScannerState.Delete();
            pool->Free(tree.ptr);
        }
    }
}

int SubtreeCompare(Subtree left, Subtree right, SubtreePool* pool) {
    pool->treeStack.Push(SubtreeToMutUnsafe(left));
    pool->treeStack.Push(SubtreeToMutUnsafe(right));

    while (pool->treeStack.size > 0) {
        right = SubtreeFromMut(pool->treeStack.Pop());
        left  = SubtreeFromMut(pool->treeStack.Pop());

        int result = 0;
        if (SubtreeSymbol(left) < SubtreeSymbol(right))
            result = -1;
        else if (SubtreeSymbol(right) < SubtreeSymbol(left))
            result = 1;
        else if (SubtreeChildCount(left) < SubtreeChildCount(right))
            result = -1;
        else if (SubtreeChildCount(right) < SubtreeChildCount(left))
            result = 1;
        if (result != 0) {
            pool->treeStack.Clear();
            return result;
        }

        for (std::uint32_t i = SubtreeChildCount(left); i > 0; i--) {
            const Subtree leftChild  = SubtreeChildren(left)[i - 1];
            const Subtree rightChild = SubtreeChildren(right)[i - 1];
            pool->treeStack.Push(SubtreeToMutUnsafe(leftChild));
            pool->treeStack.Push(SubtreeToMutUnsafe(rightChild));
        }
    }

    return 0;
}

Subtree SubtreeEdit(Subtree self, const InputEdit& inputEdit, SubtreePool* pool) {
    struct Edit {
        Length start;
        Length oldEnd;
        Length newEnd;
    };
    struct EditEntry {
        Subtree* tree;
        Edit     edit;
    };

    RawArray<EditEntry> stack{};
    stack.Push(EditEntry{
        .tree = &self,
        .edit =
            Edit{
                .start  = {inputEdit.startByte, inputEdit.startPoint},
                .oldEnd = {inputEdit.oldEndByte, inputEdit.oldEndPoint},
                .newEnd = {inputEdit.newEndByte, inputEdit.newEndPoint},
            },
    });

    while (stack.size > 0) {
        const EditEntry entry                 = stack.Pop();
        Edit            edit                  = entry.edit;
        const bool      isNoop                = edit.oldEnd.bytes == edit.start.bytes && edit.newEnd.bytes == edit.start.bytes;
        const bool      isPureInsertion       = edit.oldEnd.bytes == edit.start.bytes;
        const bool      parentDependsOnColumn = SubtreeDependsOnColumn(*entry.tree);
        const bool      columnShifted         = edit.newEnd.extent.column != edit.oldEnd.extent.column;

        Length              size           = SubtreeSize(*entry.tree);
        Length              padding        = SubtreePadding(*entry.tree);
        const Length        totalSize      = LengthAdd(padding, size);
        const std::uint32_t lookaheadBytes = SubtreeLookaheadBytes(*entry.tree);
        const std::uint32_t endByte        = totalSize.bytes + lookaheadBytes;
        if (edit.start.bytes > endByte || (isNoop && edit.start.bytes == endByte))
            continue;

        // Entirely within the space before this subtree: shift it over.
        if (edit.oldEnd.bytes <= padding.bytes) {
            padding = LengthAdd(edit.newEnd, LengthSub(padding, edit.oldEnd));
        }

        // Starts before this subtree and extends into it: shrink the content.
        else if (edit.start.bytes < padding.bytes) {
            size    = LengthSaturatingSub(size, LengthSub(edit.oldEnd, padding));
            padding = edit.newEnd;
        }

        // Within this subtree: resize it.
        else if (edit.start.bytes < totalSize.bytes || (edit.start.bytes == totalSize.bytes && isPureInsertion)) {
            size = LengthAdd(LengthSub(edit.newEnd, padding), LengthSaturatingSub(totalSize, edit.oldEnd));
        }

        MutableSubtree result = SubtreeMakeMut(pool, *entry.tree);

        if (result.data.isInline) {
            if (SubtreeCanInline(padding, size, lookaheadBytes)) {
                result.data.paddingBytes   = static_cast<std::uint8_t>(padding.bytes);
                result.data.paddingRows    = static_cast<std::uint8_t>(padding.extent.row & 0x0F);
                result.data.paddingColumns = static_cast<std::uint8_t>(padding.extent.column);
                result.data.sizeBytes      = static_cast<std::uint8_t>(size.bytes);
            }
            else {
                SubtreeHeapData* data               = pool->Allocate();
                data->refCount                      = 1;
                data->padding                       = padding;
                data->size                          = size;
                data->lookaheadBytes                = lookaheadBytes;
                data->errorCost                     = 0;
                data->childCount                    = 0;
                data->symbol                        = result.data.symbol;
                data->parseState                    = result.data.parseState;
                data->visible                       = result.data.visible;
                data->named                         = result.data.named;
                data->extra                         = result.data.extra;
                data->fragileLeft                   = false;
                data->fragileRight                  = false;
                data->hasChanges                    = false;
                data->hasExternalTokens             = false;
                data->hasExternalScannerStateChange = false;
                data->dependsOnColumn               = false;
                data->isMissing                     = result.data.isMissing;
                data->isKeyword                     = result.data.isKeyword;
                result.ptr                          = data;
            }
        }
        else {
            result.ptr->padding = padding;
            result.ptr->size    = size;
        }

        if (result.data.isInline) {
            result.data.hasChanges = true;
        }
        else {
            result.ptr->hasChanges = true;
        }
        *entry.tree = SubtreeFromMut(result);

        Length childLeft;
        Length childRight = LengthZero();
        for (std::uint32_t i = 0, n = SubtreeChildCount(*entry.tree); i < n; i++) {
            Subtree*     child     = &SubtreeChildren(*entry.tree)[i];
            const Length childSize = SubtreeTotalSize(*child);
            childLeft              = childRight;
            childRight             = LengthAdd(childLeft, childSize);

            // If this child ends before the edit, it is not affected.
            if (childRight.bytes + SubtreeLookaheadBytes(*child) < edit.start.bytes)
                continue;

            // Stop at a node starting after the edit — unless column
            // dependence forces invalidating further, up to a line break.
            if (((childLeft.bytes > edit.oldEnd.bytes) ||
                 (childLeft.bytes == edit.oldEnd.bytes && childSize.bytes > 0 && i > 0)) &&
                (!parentDependsOnColumn || childLeft.extent.row > padding.extent.row) &&
                (!SubtreeDependsOnColumn(*child) || !columnShifted || childLeft.extent.row > edit.oldEnd.extent.row)) {
                break;
            }

            // Transform the edit into the child's coordinate space.
            Edit childEdit = {
                .start  = LengthSaturatingSub(edit.start, childLeft),
                .oldEnd = LengthSaturatingSub(edit.oldEnd, childLeft),
                .newEnd = LengthSaturatingSub(edit.newEnd, childLeft),
            };

            // All inserted text applies to the first child touching the
            // edit; later children only shrink.
            if (childRight.bytes > edit.start.bytes || (childRight.bytes == edit.start.bytes && isPureInsertion)) {
                edit.newEnd = edit.start;
            }
            else {
                childEdit.oldEnd = childEdit.start;
                childEdit.newEnd = childEdit.start;
            }

            stack.Push(EditEntry{.tree = child, .edit = childEdit});
        }
    }

    stack.Delete();
    return self;
}

Subtree SubtreeLastExternalToken(Subtree tree) {
    if (!SubtreeHasExternalTokens(tree))
        return kNullSubtree;
    while (tree.ptr->childCount > 0) {
        for (std::uint32_t i = tree.ptr->childCount - 1; i + 1 > 0; i--) {
            const Subtree child = SubtreeChildren(tree)[i];
            if (SubtreeHasExternalTokens(child)) {
                tree = child;
                break;
            }
        }
    }
    return tree;
}

const ExternalScannerState* SubtreeExternalScannerState(Subtree self) {
    static const ExternalScannerState emptyState = {{.shortData = {0}}, 0};
    if (self.ptr != nullptr && !self.data.isInline && self.ptr->hasExternalTokens && self.ptr->childCount == 0)
        return &self.ptr->externalScannerState;
    return &emptyState;
}

bool SubtreeExternalScannerStateEq(Subtree self, Subtree other) {
    const ExternalScannerState* stateSelf  = SubtreeExternalScannerState(self);
    const ExternalScannerState* stateOther = SubtreeExternalScannerState(other);
    return stateSelf->Eq(stateOther->Data(), stateOther->length);
}

} // namespace ned::editor::parse
