#pragma once

#include <cstdint>

#include "Editor/Parse/Abi.h"
#include "Editor/Parse/RawArray.h"

// The green tree: ned's port of tree-sitter's Subtree (subtree.h/subtree.c).
//
// A Subtree is 8 bytes — either an inline leaf (small token, no external
// state) or a pointer to a refcounted SubtreeHeapData whose children are
// allocated immediately BEFORE the header in one buffer. The `isInline` bit
// occupies the least-significant bit of the pointer representation, which is
// always 0 for a real allocation. Nodes are immutable once shared (refcount
// > 1); MakeMut clones on sharing. This is what makes incremental reparses
// structurally share unchanged subtrees — and, later, what gives ned stable
// node identity across reparses (a reused subtree IS the same heap object).
//
// Engine-internal: raw allocation is the design here (children-before-header
// single buffers, an intrusive refcount), a deliberate exception to the
// codebase's no-raw-owning-pointers rule, held safe by the conformance and
// sanitizer suites.

namespace ned::editor::parse {

inline constexpr std::uint16_t kTreeStateNone           = 0xFFFF;
inline constexpr unsigned      kErrorCostPerRecovery    = 500;
inline constexpr unsigned      kErrorCostPerMissingTree = 110;
inline constexpr unsigned      kErrorCostPerSkippedTree = 100;
inline constexpr unsigned      kErrorCostPerSkippedLine = 30;
inline constexpr unsigned      kErrorCostPerSkippedChar = 1;

struct Length {
    std::uint32_t bytes;
    abi::Point    extent;
};

inline constexpr Length kLengthUndefined = {0, {0, 1}};

inline bool LengthIsUndefined(Length length) {
    return length.bytes == 0 && length.extent.column != 0;
}

inline abi::Point PointAdd(abi::Point a, abi::Point b) {
    if (b.row > 0)
        return {a.row + b.row, b.column};
    return {a.row, a.column + b.column};
}

inline abi::Point PointSub(abi::Point a, abi::Point b) {
    if (a.row > b.row)
        return {a.row - b.row, a.column};
    return {0, a.column > b.column ? a.column - b.column : 0};
}

inline Length LengthAdd(Length a, Length b) {
    return {a.bytes + b.bytes, PointAdd(a.extent, b.extent)};
}

inline Length LengthSub(Length a, Length b) {
    return {a.bytes >= b.bytes ? a.bytes - b.bytes : 0, PointSub(a.extent, b.extent)};
}

inline Length LengthZero() {
    return {0, {0, 0}};
}

inline Length LengthSaturatingSub(Length a, Length b) {
    return a.bytes > b.bytes ? LengthSub(a, b) : LengthZero();
}

// Serialized external-scanner state attached to an external token.
struct ExternalScannerState {
    union {
        char* longData;
        char  shortData[24];
    };
    std::uint32_t length;

    void                               Init(const char* data, unsigned dataLength);
    [[nodiscard]] const char*          Data() const;
    [[nodiscard]] bool                 Eq(const char* buffer, unsigned bufferLength) const;
    [[nodiscard]] ExternalScannerState Copy() const;
    void                               Delete();
};

struct SubtreeInlineData {
    bool          isInline : 1;
    bool          visible : 1;
    bool          named : 1;
    bool          extra : 1;
    bool          hasChanges : 1;
    bool          isMissing : 1;
    bool          isKeyword : 1;
    std::uint8_t  symbol;
    std::uint16_t parseState;
    std::uint8_t  paddingColumns;
    std::uint8_t  paddingRows : 4;
    std::uint8_t  lookaheadBytes : 4;
    std::uint8_t  paddingBytes;
    std::uint8_t  sizeBytes;
};

struct SubtreeHeapData {
    volatile std::uint32_t refCount;
    Length                 padding;
    Length                 size;
    std::uint32_t          lookaheadBytes;
    std::uint32_t          errorCost;
    std::uint32_t          childCount;
    abi::Symbol            symbol;
    abi::StateId           parseState;

    bool visible : 1;
    bool named : 1;
    bool extra : 1;
    bool fragileLeft : 1;
    bool fragileRight : 1;
    bool hasChanges : 1;
    bool hasExternalTokens : 1;
    bool hasExternalScannerStateChange : 1;
    bool dependsOnColumn : 1;
    bool isMissing : 1;
    bool isKeyword : 1;

    union {
        // Non-terminal subtrees (childCount > 0)
        struct {
            std::uint32_t visibleChildCount;
            std::uint32_t namedChildCount;
            std::uint32_t visibleDescendantCount;
            std::int32_t  dynamicPrecedence;
            std::uint16_t repeatDepth;
            std::uint16_t productionId;
            struct {
                abi::Symbol  symbol;
                abi::StateId parseState;
            } firstLeaf;
        };
        // External terminal subtrees (childCount == 0 && hasExternalTokens)
        ExternalScannerState externalScannerState;
        // Error terminal subtrees (childCount == 0 && symbol == kBuiltinSymbolError)
        std::int32_t lookaheadChar;
    };
};

union Subtree {
    SubtreeInlineData      data;
    const SubtreeHeapData* ptr;
};

union MutableSubtree {
    SubtreeInlineData data;
    SubtreeHeapData*  ptr;
};

inline constexpr Subtree kNullSubtree = {.ptr = nullptr};

using SubtreeArray        = RawArray<Subtree>;
using MutableSubtreeArray = RawArray<MutableSubtree>;

struct SubtreePool {
    MutableSubtreeArray freeTrees;
    MutableSubtreeArray treeStack;

    static SubtreePool New(std::uint32_t capacity);
    void               Delete();
    SubtreeHeapData*   Allocate();
    void               Free(SubtreeHeapData* tree);
};

inline Subtree SubtreeFromMut(MutableSubtree self) {
    Subtree result;
    result.data = self.data;
    return result;
}

inline MutableSubtree SubtreeToMutUnsafe(Subtree self) {
    MutableSubtree result;
    result.data = self.data;
    return result;
}

inline abi::Symbol SubtreeSymbol(Subtree self) {
    return self.data.isInline ? self.data.symbol : self.ptr->symbol;
}
inline bool SubtreeVisible(Subtree self) {
    return self.data.isInline ? self.data.visible : self.ptr->visible;
}
inline bool SubtreeNamed(Subtree self) {
    return self.data.isInline ? self.data.named : self.ptr->named;
}
inline bool SubtreeExtra(Subtree self) {
    return self.data.isInline ? self.data.extra : self.ptr->extra;
}
inline bool SubtreeHasChanges(Subtree self) {
    return self.data.isInline ? self.data.hasChanges : self.ptr->hasChanges;
}
inline bool SubtreeMissing(Subtree self) {
    return self.data.isInline ? self.data.isMissing : self.ptr->isMissing;
}
inline bool SubtreeIsKeyword(Subtree self) {
    return self.data.isInline ? self.data.isKeyword : self.ptr->isKeyword;
}
inline abi::StateId SubtreeParseState(Subtree self) {
    return self.data.isInline ? self.data.parseState : self.ptr->parseState;
}
inline std::uint32_t SubtreeLookaheadBytes(Subtree self) {
    return self.data.isInline ? self.data.lookaheadBytes : self.ptr->lookaheadBytes;
}

inline std::size_t SubtreeAllocSize(std::uint32_t childCount) {
    return childCount * sizeof(Subtree) + sizeof(SubtreeHeapData);
}

inline Subtree* SubtreeChildren(Subtree self) {
    return self.data.isInline ? nullptr
                              : reinterpret_cast<Subtree*>(const_cast<SubtreeHeapData*>(self.ptr)) - self.ptr->childCount;
}

inline Subtree* SubtreeChildren(MutableSubtree self) {
    return SubtreeChildren(SubtreeFromMut(self));
}

inline void SubtreeSetExtra(MutableSubtree* self, bool isExtra) {
    if (self->data.isInline) {
        self->data.extra = isExtra;
    }
    else {
        self->ptr->extra = isExtra;
    }
}

inline abi::Symbol SubtreeLeafSymbol(Subtree self) {
    if (self.data.isInline)
        return self.data.symbol;
    if (self.ptr->childCount == 0)
        return self.ptr->symbol;
    return self.ptr->firstLeaf.symbol;
}

inline abi::StateId SubtreeLeafParseState(Subtree self) {
    if (self.data.isInline)
        return self.data.parseState;
    if (self.ptr->childCount == 0)
        return self.ptr->parseState;
    return self.ptr->firstLeaf.parseState;
}

inline Length SubtreePadding(Subtree self) {
    if (self.data.isInline)
        return {self.data.paddingBytes, {self.data.paddingRows, self.data.paddingColumns}};
    return self.ptr->padding;
}

inline Length SubtreeSize(Subtree self) {
    if (self.data.isInline)
        return {self.data.sizeBytes, {0, self.data.sizeBytes}};
    return self.ptr->size;
}

inline Length SubtreeTotalSize(Subtree self) {
    return LengthAdd(SubtreePadding(self), SubtreeSize(self));
}
inline std::uint32_t SubtreeTotalBytes(Subtree self) {
    return SubtreeTotalSize(self).bytes;
}
inline std::uint32_t SubtreeChildCount(Subtree self) {
    return self.data.isInline ? 0 : self.ptr->childCount;
}
inline std::uint32_t SubtreeRepeatDepth(Subtree self) {
    return self.data.isInline ? 0 : self.ptr->repeatDepth;
}

inline std::uint32_t SubtreeVisibleDescendantCount(Subtree self) {
    return (self.data.isInline || self.ptr->childCount == 0) ? 0 : self.ptr->visibleDescendantCount;
}

inline std::uint32_t SubtreeVisibleChildCount(Subtree self) {
    return SubtreeChildCount(self) > 0 ? self.ptr->visibleChildCount : 0;
}

inline std::uint32_t SubtreeErrorCost(Subtree self) {
    if (SubtreeMissing(self))
        return kErrorCostPerMissingTree + kErrorCostPerRecovery;
    return self.data.isInline ? 0 : self.ptr->errorCost;
}

inline std::int32_t SubtreeDynamicPrecedence(Subtree self) {
    return (self.data.isInline || self.ptr->childCount == 0) ? 0 : self.ptr->dynamicPrecedence;
}

inline std::uint16_t SubtreeProductionId(Subtree self) {
    return SubtreeChildCount(self) > 0 ? self.ptr->productionId : 0;
}

inline bool SubtreeFragileLeft(Subtree self) {
    return self.data.isInline ? false : self.ptr->fragileLeft;
}
inline bool SubtreeFragileRight(Subtree self) {
    return self.data.isInline ? false : self.ptr->fragileRight;
}
inline bool SubtreeIsFragile(Subtree self) {
    return self.data.isInline ? false : (self.ptr->fragileLeft || self.ptr->fragileRight);
}
inline bool SubtreeHasExternalTokens(Subtree self) {
    return self.data.isInline ? false : self.ptr->hasExternalTokens;
}
inline bool SubtreeHasExternalScannerStateChange(Subtree self) {
    return self.data.isInline ? false : self.ptr->hasExternalScannerStateChange;
}
inline bool SubtreeDependsOnColumn(Subtree self) {
    return self.data.isInline ? false : self.ptr->dependsOnColumn;
}
inline bool SubtreeIsError(Subtree self) {
    return SubtreeSymbol(self) == abi::kBuiltinSymbolError;
}
inline bool SubtreeIsEof(Subtree self) {
    return SubtreeSymbol(self) == abi::kBuiltinSymbolEnd;
}

// --- Allocating / mutating operations (Green.cpp) ---------------------------

Subtree                     SubtreeNewLeaf(SubtreePool* pool, abi::Symbol symbol, Length padding, Length size, std::uint32_t lookaheadBytes,
                                           abi::StateId parseState, bool hasExternalTokens, bool dependsOnColumn, bool isKeyword,
                                           const abi::LanguageData* language);
Subtree                     SubtreeNewError(SubtreePool* pool, std::int32_t lookaheadChar, Length padding, Length size,
                                            std::uint32_t bytesScanned, abi::StateId parseState, const abi::LanguageData* language);
MutableSubtree              SubtreeNewNode(abi::Symbol symbol, SubtreeArray* children, unsigned productionId,
                                           const abi::LanguageData* language);
Subtree                     SubtreeNewErrorNode(SubtreeArray* children, bool extra, const abi::LanguageData* language);
Subtree                     SubtreeNewMissingLeaf(SubtreePool* pool, abi::Symbol symbol, Length padding, std::uint32_t lookaheadBytes,
                                                  const abi::LanguageData* language);
MutableSubtree              SubtreeClone(Subtree self);
MutableSubtree              SubtreeMakeMut(SubtreePool* pool, Subtree self);
void                        SubtreeRetain(Subtree self);
void                        SubtreeRelease(SubtreePool* pool, Subtree self);
int                         SubtreeCompare(Subtree left, Subtree right, SubtreePool* pool);
void                        SubtreeSetSymbol(MutableSubtree* self, abi::Symbol symbol, const abi::LanguageData* language);
void                        SubtreeCompress(MutableSubtree self, unsigned count, const abi::LanguageData* language, MutableSubtreeArray* stack);
void                        SubtreeSummarizeChildren(MutableSubtree self, const abi::LanguageData* language);
Subtree                     SubtreeLastExternalToken(Subtree tree);
const ExternalScannerState* SubtreeExternalScannerState(Subtree self);
bool                        SubtreeExternalScannerStateEq(Subtree self, Subtree other);

void SubtreeArrayCopy(SubtreeArray self, SubtreeArray* dest);
void SubtreeArrayClear(SubtreePool* pool, SubtreeArray* self);
void SubtreeArrayDelete(SubtreePool* pool, SubtreeArray* self);
void SubtreeArrayRemoveTrailingExtras(SubtreeArray* self, SubtreeArray* destination);
void SubtreeArrayReverse(SubtreeArray* self);

// A text edit in the coordinates SubtreeEdit expects (TSInputEdit's shape).
struct InputEdit {
    std::uint32_t startByte;
    std::uint32_t oldEndByte;
    std::uint32_t newEndByte;
    abi::Point    startPoint;
    abi::Point    oldEndPoint;
    abi::Point    newEndPoint;
};

// Adjust the tree's byte layout for an edit, marking touched paths
// hasChanges. Takes ownership of `self`; shared nodes on the edited path are
// cloned (clone-on-share), so other holders of the old tree are unaffected.
Subtree SubtreeEdit(Subtree self, const InputEdit& edit, SubtreePool* pool);

} // namespace ned::editor::parse
