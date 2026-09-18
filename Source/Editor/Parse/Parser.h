#pragma once

#include <string_view>

#include "Editor/Parse/Abi.h"
#include "Editor/Parse/Green.h"
#include "Editor/Parse/LanguageTables.h"
#include "Editor/Parse/Lexer.h"
#include "Editor/Parse/RawArray.h"
#include "Editor/Parse/Stack.h"
#include "Editor/Parse/Tree.h"

// Ned's parser runtime — the port of tree-sitter's parser.c, driving a
// generated grammar's ABI-15 tables, lex functions and external scanner
// against ned's own green tree and GLR stack. `Engine` is deliberately
// distinct from the editor-facing `grammar::Parser` wrapper, which will
// sit on top of this at the M5 swap.

namespace ned::editor::parse {

class Engine {
  public:
    // `language` is a loaded package's abi::LanguageData (Grammar/
    // LanguagePackage.h); its table version is checked (throws
    // std::runtime_error).
    explicit Engine(const void* language);
    ~Engine();
    Engine(const Engine&)            = delete;
    Engine& operator=(const Engine&) = delete;

    // Parse the given text from scratch.
    GreenTree Parse(std::string_view text);

    // Incremental: reuse unchanged subtrees of `oldTree`, which must have
    // been edited (GreenTree::WithEdit) to match `text`'s byte layout.
    GreenTree Parse(std::string_view text, const GreenTree& oldTree);

  private:
    struct TokenCache {
        Subtree       token             = kNullSubtree;
        Subtree       lastExternalToken = kNullSubtree;
        std::uint32_t byteIndex         = 0;
    };

    struct ReusableNodeEntry {
        Subtree       tree;
        std::uint32_t childIndex;
        std::uint32_t byteOffset;
    };

    struct ReusableNode {
        RawArray<ReusableNodeEntry> stack;
        Subtree                     lastExternalToken = kNullSubtree;
    };

    struct ReduceActionEntry {
        std::uint32_t  count;
        abi::Symbol    symbol;
        int            dynamicPrecedence;
        unsigned short productionId;
    };

    struct ErrorStatus {
        unsigned cost;
        unsigned nodeCount;
        int      dynamicPrecedence;
        bool     isInError;
    };

    enum class ErrorComparison : std::uint8_t {
        TakeLeft,
        PreferLeft,
        None,
        PreferRight,
        TakeRight,
    };

    void            Reset();
    bool            BreakdownTopOfStack(StackVersion version);
    void            BreakdownLookahead(Subtree* lookahead, abi::StateId state, ReusableNode* reusableNode);
    ErrorComparison CompareVersions(ErrorStatus a, ErrorStatus b) const;
    ErrorStatus     VersionStatus(StackVersion version);
    bool            BetterVersionExists(StackVersion version, bool isInError, unsigned cost);
    void            ExternalScannerCreate();
    void            ExternalScannerDestroy();
    unsigned        ExternalScannerSerialize();
    void            ExternalScannerDeserialize(Subtree externalToken);
    bool            ExternalScannerScan(abi::StateId externalLexState);
    bool            CanReuseFirstLeaf(abi::StateId state, Subtree tree, TableEntry* tableEntry);
    Subtree         LexToken(StackVersion version, abi::StateId parseState);
    Subtree         GetCachedToken(abi::StateId state, std::size_t position, Subtree lastExternalToken, TableEntry* tableEntry);
    void            SetCachedToken(std::uint32_t byteIndex, Subtree lastExternalToken, Subtree token);
    Subtree         ReuseNode(StackVersion version, abi::StateId* state, std::uint32_t position, Subtree lastExternalToken,
                              TableEntry* tableEntry);
    bool            SelectTree(Subtree left, Subtree right);
    bool            SelectChildren(Subtree left, const SubtreeArray* children);
    void            Shift(StackVersion version, abi::StateId state, Subtree lookahead, bool extra);
    StackVersion    Reduce(StackVersion version, abi::Symbol symbol, std::uint32_t count, int dynamicPrecedence,
                           std::uint16_t productionId, bool isFragile, bool endOfNonTerminalExtra);
    void            Accept(StackVersion version, Subtree lookahead);
    bool            DoAllPotentialReductions(StackVersion startingVersion, abi::Symbol lookaheadSymbol);
    bool            RecoverToState(StackVersion version, unsigned depth, abi::StateId goalState);
    void            Recover(StackVersion version, Subtree lookahead);
    void            HandleError(StackVersion version, Subtree lookahead);
    bool            Advance(StackVersion version, bool allowNodeReuse);
    unsigned        CondenseStack();
    void            BalanceSubtree();

    static void          ReusableNodeClear(ReusableNode* self);
    static Subtree       ReusableNodeTree(ReusableNode* self);
    static std::uint32_t ReusableNodeByteOffset(ReusableNode* self);
    static void          ReusableNodeAdvance(ReusableNode* self);
    static bool          ReusableNodeDescend(ReusableNode* self);
    static void          ReusableNodeAdvancePastLeaf(ReusableNode* self);

    const abi::LanguageData*    language_;
    Lexer                       lexer_;
    SubtreePool                 treePool_;
    Stack*                      stack_;
    RawArray<ReduceActionEntry> reduceActions_;
    Subtree                     finishedTree_ = kNullSubtree;
    SubtreeArray                trailingExtras_;
    SubtreeArray                trailingExtras2_;
    SubtreeArray                scratchTrees_;
    TokenCache                  tokenCache_;
    ReusableNode                reusableNode_;
    void*                       externalScannerPayload_ = nullptr;
    Subtree                     oldTree_                = kNullSubtree;
    unsigned                    acceptCount_            = 0;
};

} // namespace ned::editor::parse
