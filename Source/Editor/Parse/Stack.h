#pragma once

#include <cstdint>

#include "Editor/Parse/Green.h"
#include "Editor/Parse/RawArray.h"

// The GLR parse stack — the port of tree-sitter's stack.c. A directed graph
// of nodes (state + position + up to 8 predecessor links); each "version" is
// one live head. Versions split on conflicts, merge when they converge, and
// pop operations return one slice per distinct path.

namespace ned::editor::parse {

using StackVersion                              = unsigned;
inline constexpr StackVersion kStackVersionNone = static_cast<StackVersion>(-1);

struct StackSlice {
    SubtreeArray subtrees;
    StackVersion version;
};
using StackSliceArray = RawArray<StackSlice>;

struct StackSummaryEntry {
    Length       position;
    unsigned     depth;
    abi::StateId state;
};
using StackSummary = RawArray<StackSummaryEntry>;

struct StackNode;

struct StackLink {
    StackNode* node;
    Subtree    subtree;
    bool       isPending;
};

inline constexpr unsigned kMaxLinkCount = 8;

struct StackNode {
    abi::StateId   state;
    Length         position;
    StackLink      links[kMaxLinkCount];
    unsigned short linkCount;
    std::uint32_t  refCount;
    unsigned       errorCost;
    unsigned       nodeCount;
    int            dynamicPrecedence;
};

struct StackIterator {
    StackNode*    node;
    SubtreeArray  subtrees;
    std::uint32_t subtreeCount;
    bool          isPending;
};

enum class StackStatus : std::uint8_t {
    Active,
    Paused,
    Halted,
};

struct StackHead {
    StackNode*    node;
    StackSummary* summary;
    unsigned      nodeCountAtLastError;
    Subtree       lastExternalToken;
    Subtree       lookaheadWhenPaused;
    StackStatus   status;
};

using StackAction                             = unsigned;
inline constexpr StackAction kStackActionNone = 0;
inline constexpr StackAction kStackActionStop = 1;
inline constexpr StackAction kStackActionPop  = 2;

class Stack {
  public:
    explicit Stack(SubtreePool* subtreePool);
    ~Stack();
    Stack(const Stack&)            = delete;
    Stack& operator=(const Stack&) = delete;

    [[nodiscard]] std::uint32_t VersionCount() const {
        return heads_.size;
    }
    std::uint32_t              HaltedVersionCount();
    [[nodiscard]] abi::StateId State(StackVersion version) const {
        return heads_[version].node->state;
    }
    [[nodiscard]] Length Position(StackVersion version) const {
        return heads_[version].node->position;
    }
    [[nodiscard]] Subtree LastExternalToken(StackVersion version) const {
        return heads_[version].lastExternalToken;
    }
    void                   SetLastExternalToken(StackVersion version, Subtree token);
    [[nodiscard]] unsigned ErrorCost(StackVersion version) const;
    unsigned               NodeCountSinceError(StackVersion version);
    void                   Push(StackVersion version, Subtree subtree, bool pending, abi::StateId state);
    StackSliceArray        PopCount(StackVersion version, std::uint32_t count);
    SubtreeArray           PopError(StackVersion version);
    StackSliceArray        PopPending(StackVersion version);
    StackSliceArray        PopAll(StackVersion version);
    void                   RecordSummary(StackVersion version, unsigned maxDepth);
    StackSummary*          GetSummary(StackVersion version) {
        return heads_[version].summary;
    }
    [[nodiscard]] int DynamicPrecedence(StackVersion version) const {
        return heads_[version].node->dynamicPrecedence;
    }
    [[nodiscard]] bool HasAdvancedSinceError(StackVersion version) const;
    bool               Merge(StackVersion version1, StackVersion version2);
    [[nodiscard]] bool CanMerge(StackVersion version1, StackVersion version2) const;
    Subtree            Resume(StackVersion version);
    void               Pause(StackVersion version, Subtree lookahead);
    void               Halt(StackVersion version) {
        heads_[version].status = StackStatus::Halted;
    }
    [[nodiscard]] bool IsActive(StackVersion version) const {
        return heads_[version].status == StackStatus::Active;
    }
    [[nodiscard]] bool IsPaused(StackVersion version) const {
        return heads_[version].status == StackStatus::Paused;
    }
    [[nodiscard]] bool IsHalted(StackVersion version) const {
        return heads_[version].status == StackStatus::Halted;
    }
    void         RenumberVersion(StackVersion v1, StackVersion v2);
    void         SwapVersions(StackVersion v1, StackVersion v2);
    StackVersion CopyVersion(StackVersion version);
    void         RemoveVersion(StackVersion version);
    void         Clear();

  private:
    using StackNodeArray = RawArray<StackNode*>;
    using Callback       = StackAction (*)(void*, const StackIterator*);

    StackVersion    AddVersion(StackVersion originalVersion, StackNode* node);
    void            AddSlice(StackVersion originalVersion, StackNode* node, SubtreeArray* subtrees);
    StackSliceArray Iterate(StackVersion version, Callback callback, void* payload, int goalSubtreeCount);

    RawArray<StackHead>     heads_;
    StackSliceArray         slices_;
    RawArray<StackIterator> iterators_;
    StackNodeArray          nodePool_;
    StackNode*              baseNode_;
    SubtreePool*            subtreePool_;
};

} // namespace ned::editor::parse
