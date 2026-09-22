#include "Editor/Parse/Parser.h"

#include "Editor/Parse/LexDfa.h"

#include <algorithm>

#include <cstring>
#include <stdexcept>

namespace ned::editor::parse {

namespace {

    // Bounds on the EOF completion pass (Engine::CloseOpenConstructsAtEof).
    // Each closure leaves the stack strictly shallower, so the outer loop
    // terminates on its own and kMaxEofClosures only caps what a
    // pathologically nested document can ask for. The other two bound the
    // search for ONE closer: how many terminals it may spell (XML and JSX
    // need three -- "</", the name, ">"), and how many candidates it tries
    // at each position of that spelling.
    constexpr unsigned kMaxEofClosures  = 64;
    constexpr unsigned kMaxCloserTokens = 3;
    constexpr unsigned kMaxCloserBranch = 4;
    // A closer that takes more than one token spells itself out the same way
    // every time ("</" is followed by a name, a name by ">"), so the
    // positions after the first barely need to branch at all.
    constexpr unsigned kMaxCloserBranchDeeper = 2;
    // Constructs one PARSE will close at EOF, across every version and every
    // pass. Each closure already leaves the stack strictly shallower; this
    // bounds what a badly broken document can draw anyway.
    constexpr unsigned kMaxEofClosuresPerParse = 128;
    // Reductions ParseCanFinish will chain before giving up on the answer.
    constexpr unsigned kMaxFinishReductions     = 256;
    constexpr unsigned kMaxVersionCount         = 6;
    constexpr unsigned kMaxVersionCountOverflow = 4;
    constexpr unsigned kMaxSummaryDepth         = 16;
    constexpr unsigned kMaxCostDifference       = 18 * kErrorCostPerSkippedTree;

} // namespace

// --- ReusableNode -----------------------------------------------------------

void Engine::ReusableNodeClear(ReusableNode* self) {
    self->stack.Clear();
    self->lastExternalToken = kNullSubtree;
}

Subtree Engine::ReusableNodeTree(ReusableNode* self) {
    return self->stack.size > 0 ? self->stack[self->stack.size - 1].tree : kNullSubtree;
}

std::uint32_t Engine::ReusableNodeByteOffset(ReusableNode* self) {
    return self->stack.size > 0 ? self->stack[self->stack.size - 1].byteOffset : 0xFFFFFFFF;
}

void Engine::ReusableNodeAdvance(ReusableNode* self) {
    const ReusableNodeEntry lastEntry  = self->stack.Back();
    const std::uint32_t     byteOffset = lastEntry.byteOffset + SubtreeTotalBytes(lastEntry.tree);
    if (SubtreeHasExternalTokens(lastEntry.tree))
        self->lastExternalToken = SubtreeLastExternalToken(lastEntry.tree);

    Subtree       tree;
    std::uint32_t nextIndex = 0;
    do {
        const ReusableNodeEntry poppedEntry = self->stack.Pop();
        nextIndex                           = poppedEntry.childIndex + 1;
        if (self->stack.size == 0)
            return;
        tree = self->stack.Back().tree;
    }
    while (SubtreeChildCount(tree) <= nextIndex);

    self->stack.Push(ReusableNodeEntry{
        .tree       = SubtreeChildren(tree)[nextIndex],
        .childIndex = nextIndex,
        .byteOffset = byteOffset,
    });
}

bool Engine::ReusableNodeDescend(ReusableNode* self) {
    const ReusableNodeEntry lastEntry = self->stack.Back();
    if (SubtreeChildCount(lastEntry.tree) > 0) {
        self->stack.Push(ReusableNodeEntry{
            .tree       = SubtreeChildren(lastEntry.tree)[0],
            .childIndex = 0,
            .byteOffset = lastEntry.byteOffset,
        });
        return true;
    }
    return false;
}

void Engine::ReusableNodeAdvancePastLeaf(ReusableNode* self) {
    while (ReusableNodeDescend(self)) {
    }
    ReusableNodeAdvance(self);
}

// --- Engine -----------------------------------------------------------------

Engine::Engine(const void* language) : language_(static_cast<const abi::LanguageData*>(language)) {
    if (language_ == nullptr || language_->abiVersion != abi::kAbiVersion)
        throw std::runtime_error("parse::Engine: language tables are not version " + std::to_string(abi::kAbiVersion));
    treePool_ = SubtreePool::New(32);
    stack_    = new Stack(&treePool_);
    reduceActions_.Reserve(4);
}

Engine::~Engine() {
    Reset();
    delete stack_;
    SetCachedToken(0, kNullSubtree, kNullSubtree);
    treePool_.Delete();
    reduceActions_.Delete();
    trailingExtras_.Delete();
    trailingExtras2_.Delete();
    scratchTrees_.Delete();
    reusableNode_.stack.Delete();
}

void Engine::Reset() {
    ExternalScannerDestroy();
    ReusableNodeClear(&reusableNode_);
    if (oldTree_.ptr != nullptr) {
        SubtreeRelease(&treePool_, oldTree_);
        oldTree_ = kNullSubtree;
    }
    lexer_.Reset(LengthZero());
    stack_->Clear();
    SetCachedToken(0, kNullSubtree, kNullSubtree);
    if (finishedTree_.ptr != nullptr) {
        SubtreeRelease(&treePool_, finishedTree_);
        finishedTree_ = kNullSubtree;
    }
    acceptCount_ = 0;
}

bool Engine::BreakdownTopOfStack(StackVersion version) {
    bool didBreakDown = false;
    bool pending      = false;

    do {
        StackSliceArray pop = stack_->PopPending(version);
        if (pop.size == 0)
            break;

        didBreakDown = true;
        pending      = false;
        for (std::uint32_t i = 0; i < pop.size; i++) {
            StackSlice    slice  = pop[i];
            abi::StateId  state  = stack_->State(slice.version);
            const Subtree parent = slice.subtrees.Front();

            for (std::uint32_t j = 0, n = SubtreeChildCount(parent); j < n; j++) {
                const Subtree child = SubtreeChildren(parent)[j];
                pending             = SubtreeChildCount(child) > 0;

                if (SubtreeIsError(child)) {
                    state = kErrorState;
                }
                else if (!SubtreeExtra(child)) {
                    state = LanguageNextState(language_, state, SubtreeSymbol(child));
                }

                SubtreeRetain(child);
                stack_->Push(slice.version, child, pending, state);
            }

            for (std::uint32_t j = 1; j < slice.subtrees.size; j++) {
                const Subtree tree = slice.subtrees[j];
                stack_->Push(slice.version, tree, false, state);
            }

            SubtreeRelease(&treePool_, parent);
            slice.subtrees.Delete();
        }
    }
    while (pending);

    return didBreakDown;
}

void Engine::BreakdownLookahead(Subtree* lookahead, abi::StateId state, ReusableNode* reusableNode) {
    bool    didDescend = false;
    Subtree tree       = ReusableNodeTree(reusableNode);
    while (SubtreeChildCount(tree) > 0 && SubtreeParseState(tree) != state) {
        ReusableNodeDescend(reusableNode);
        tree       = ReusableNodeTree(reusableNode);
        didDescend = true;
    }

    if (didDescend) {
        SubtreeRelease(&treePool_, *lookahead);
        *lookahead = tree;
        SubtreeRetain(*lookahead);
    }
}

Engine::ErrorComparison Engine::CompareVersions(ErrorStatus a, ErrorStatus b) const {
    if (!a.isInError && b.isInError) {
        if (a.cost < b.cost)
            return ErrorComparison::TakeLeft;
        return ErrorComparison::PreferLeft;
    }

    if (a.isInError && !b.isInError) {
        if (b.cost < a.cost)
            return ErrorComparison::TakeRight;
        return ErrorComparison::PreferRight;
    }

    if (a.cost < b.cost) {
        if ((b.cost - a.cost) * (1 + a.nodeCount) > kMaxCostDifference)
            return ErrorComparison::TakeLeft;
        return ErrorComparison::PreferLeft;
    }

    if (b.cost < a.cost) {
        if ((a.cost - b.cost) * (1 + b.nodeCount) > kMaxCostDifference)
            return ErrorComparison::TakeRight;
        return ErrorComparison::PreferRight;
    }

    if (a.dynamicPrecedence > b.dynamicPrecedence)
        return ErrorComparison::PreferLeft;
    if (b.dynamicPrecedence > a.dynamicPrecedence)
        return ErrorComparison::PreferRight;
    return ErrorComparison::None;
}

Engine::ErrorStatus Engine::VersionStatus(StackVersion version) {
    unsigned   cost     = stack_->ErrorCost(version);
    const bool isPaused = stack_->IsPaused(version);
    if (isPaused)
        cost += kErrorCostPerSkippedTree;
    return ErrorStatus{
        .cost              = cost,
        .nodeCount         = stack_->NodeCountSinceError(version),
        .dynamicPrecedence = stack_->DynamicPrecedence(version),
        .isInError         = isPaused || stack_->State(version) == kErrorState,
    };
}

bool Engine::BetterVersionExists(StackVersion version, bool isInError, unsigned cost) {
    if (finishedTree_.ptr != nullptr && SubtreeErrorCost(finishedTree_) <= cost)
        return true;

    const Length      position = stack_->Position(version);
    const ErrorStatus status   = {
        .cost              = cost,
        .nodeCount         = stack_->NodeCountSinceError(version),
        .dynamicPrecedence = stack_->DynamicPrecedence(version),
        .isInError         = isInError,
    };

    for (StackVersion i = 0, n = stack_->VersionCount(); i < n; i++) {
        if (i == version || !stack_->IsActive(i) || stack_->Position(i).bytes < position.bytes)
            continue;
        const ErrorStatus statusI = VersionStatus(i);
        switch (CompareVersions(status, statusI)) {
            case ErrorComparison::TakeRight:
                return true;
            case ErrorComparison::PreferRight:
                if (stack_->CanMerge(i, version))
                    return true;
                break;
            default:
                break;
        }
    }

    return false;
}

void Engine::ExternalScannerCreate() {
    if (language_->externalScanner.states != nullptr && language_->externalScanner.create != nullptr)
        externalScannerPayload_ = language_->externalScanner.create();
}

void Engine::ExternalScannerDestroy() {
    if (externalScannerPayload_ != nullptr && language_->externalScanner.destroy != nullptr)
        language_->externalScanner.destroy(externalScannerPayload_);
    externalScannerPayload_ = nullptr;
}

unsigned Engine::ExternalScannerSerialize() {
    return language_->externalScanner.serialize(externalScannerPayload_, lexer_.scratchBuffer);
}

void Engine::ExternalScannerDeserialize(Subtree externalToken) {
    const char*   data   = nullptr;
    std::uint32_t length = 0;
    if (externalToken.ptr != nullptr) {
        data   = externalToken.ptr->externalScannerState.Data();
        length = externalToken.ptr->externalScannerState.length;
    }
    language_->externalScanner.deserialize(externalScannerPayload_, data, length);
}

bool Engine::ExternalScannerScan(abi::StateId externalLexState) {
    const bool* validExternalTokens = LanguageEnabledExternalTokens(language_, externalLexState);
    return language_->externalScanner.scan(externalScannerPayload_, &lexer_.data, validExternalTokens);
}

bool Engine::CanReuseFirstLeaf(abi::StateId state, Subtree tree, TableEntry* tableEntry) {
    const abi::Symbol    leafSymbol     = SubtreeLeafSymbol(tree);
    const abi::StateId   leafState      = SubtreeLeafParseState(tree);
    const abi::LexerMode currentLexMode = LanguageLexModeForState(language_, state);
    const abi::LexerMode leafLexMode    = LanguageLexModeForState(language_, leafState);

    // At the end of a non-terminal extra node, the lexer normally returns
    // NULL; avoid reusing tokens there so incremental parsing matches.
    if (currentLexMode.lexState == static_cast<std::uint16_t>(-1))
        return false;

    // If the token was created in a state with the same set of lookaheads,
    // it is reusable.
    if (tableEntry->actionCount > 0 && std::memcmp(&leafLexMode, &currentLexMode, sizeof(abi::LexerMode)) == 0 &&
        (leafSymbol != language_->keywordCaptureToken ||
         (!SubtreeIsKeyword(tree) && SubtreeParseState(tree) == state)))
        return true;

    // Empty tokens are not reusable in states with different lookaheads.
    if (SubtreeSize(tree).bytes == 0 && leafSymbol != abi::kBuiltinSymbolEnd)
        return false;

    // If the current state allows external tokens or other tokens that
    // conflict with this token, this token is not reusable.
    return currentLexMode.externalLexState == 0 && tableEntry->isReusable;
}

Subtree Engine::LexToken(StackVersion version, abi::StateId parseState) {
    abi::LexerMode lexMode = LanguageLexModeForState(language_, parseState);
    if (lexMode.lexState == static_cast<std::uint16_t>(-1))
        return kNullSubtree;

    const Length  startPosition = stack_->Position(version);
    const Subtree externalToken = stack_->LastExternalToken(version);

    bool          foundExternalToken          = false;
    bool          errorMode                   = parseState == kErrorState;
    bool          skippedError                = false;
    bool          calledGetColumn             = false;
    std::int32_t  firstErrorCharacter         = 0;
    Length        errorStartPosition          = LengthZero();
    Length        errorEndPosition            = LengthZero();
    std::uint32_t lookaheadEndByte            = 0;
    std::uint32_t externalScannerStateLen     = 0;
    bool          externalScannerStateChanged = false;
    lexer_.Reset(startPosition);

    for (;;) {
        bool             foundToken      = false;
        const Length     currentPosition = lexer_.currentPosition;
        const ColumnData columnData      = lexer_.columnData;

        if (lexMode.externalLexState != 0) {
            lexer_.Start();
            ExternalScannerDeserialize(externalToken);
            foundToken = ExternalScannerScan(lexMode.externalLexState);
            lexer_.Finish(&lookaheadEndByte);

            if (foundToken) {
                externalScannerStateLen = ExternalScannerSerialize();
                externalScannerStateChanged =
                    !SubtreeExternalScannerState(externalToken)->Eq(lexer_.scratchBuffer, externalScannerStateLen);

                // Ignore classes of empty external tokens that would cause
                // infinite loops (error recovery, extras).
                if (lexer_.tokenEndPosition.bytes <= currentPosition.bytes && !externalScannerStateChanged) {
                    const abi::Symbol  symbol         = language_->externalScanner.symbolMap[lexer_.data.resultSymbol];
                    const abi::StateId nextParseState = LanguageNextState(language_, parseState, symbol);
                    const bool         tokenIsExtra   = nextParseState == parseState;
                    if (errorMode || !stack_->HasAdvancedSinceError(version) || tokenIsExtra)
                        foundToken = false;
                }
            }

            if (foundToken) {
                foundExternalToken = true;
                calledGetColumn    = lexer_.didGetColumn;
                break;
            }

            lexer_.Reset(currentPosition);
            lexer_.columnData = columnData;
        }

        lexer_.Start();
        foundToken = LexMain(language_, &lexer_.data, lexMode.lexState);
        lexer_.Finish(&lookaheadEndByte);
        if (foundToken)
            break;

        if (!errorMode) {
            errorMode = true;
            lexMode   = LanguageLexModeForState(language_, kErrorState);
            lexer_.Reset(startPosition);
            continue;
        }

        if (!skippedError) {
            skippedError        = true;
            errorStartPosition  = lexer_.tokenStartPosition;
            errorEndPosition    = lexer_.tokenStartPosition;
            firstErrorCharacter = lexer_.data.lookahead;
        }

        if (lexer_.currentPosition.bytes == errorEndPosition.bytes) {
            if (lexer_.AtEof()) {
                lexer_.data.resultSymbol = abi::kBuiltinSymbolError;
                break;
            }
            lexer_.data.advance(&lexer_.data, false);
        }

        errorEndPosition = lexer_.currentPosition;
    }

    Subtree result;
    if (skippedError) {
        const Length        padding        = LengthSub(errorStartPosition, startPosition);
        const Length        size           = LengthSub(errorEndPosition, errorStartPosition);
        const std::uint32_t lookaheadBytes = lookaheadEndByte - errorEndPosition.bytes;
        result                             = SubtreeNewError(&treePool_, firstErrorCharacter, padding, size, lookaheadBytes, parseState, language_);
    }
    else {
        bool                isKeyword      = false;
        abi::Symbol         symbol         = lexer_.data.resultSymbol;
        const Length        padding        = LengthSub(lexer_.tokenStartPosition, startPosition);
        const Length        size           = LengthSub(lexer_.tokenEndPosition, lexer_.tokenStartPosition);
        const std::uint32_t lookaheadBytes = lookaheadEndByte - lexer_.tokenEndPosition.bytes;

        if (foundExternalToken) {
            symbol = language_->externalScanner.symbolMap[symbol];
        }
        else if (symbol == language_->keywordCaptureToken && symbol != 0) {
            const std::uint32_t endByte = lexer_.tokenEndPosition.bytes;
            lexer_.Reset(lexer_.tokenStartPosition);
            lexer_.Start();

            isKeyword = LexKeyword(language_, &lexer_.data, 0);

            if (isKeyword && lexer_.tokenEndPosition.bytes == endByte &&
                (LanguageHasActions(language_, parseState, lexer_.data.resultSymbol) ||
                 LanguageIsReservedWord(language_, parseState, lexer_.data.resultSymbol))) {
                symbol = lexer_.data.resultSymbol;
            }
        }

        result = SubtreeNewLeaf(&treePool_, symbol, padding, size, lookaheadBytes, parseState, foundExternalToken,
                                calledGetColumn, isKeyword, language_);

        if (foundExternalToken) {
            auto* mutResult = const_cast<SubtreeHeapData*>(result.ptr);
            mutResult->externalScannerState.Init(lexer_.scratchBuffer, externalScannerStateLen);
            mutResult->hasExternalScannerStateChange = externalScannerStateChanged;
        }
    }

    return result;
}

Subtree Engine::GetCachedToken(abi::StateId state, std::size_t position, Subtree lastExternalToken,
                               TableEntry* tableEntry) {
    TokenCache* cache = &tokenCache_;
    if (cache->token.ptr != nullptr && cache->byteIndex == position &&
        SubtreeExternalScannerStateEq(cache->lastExternalToken, lastExternalToken)) {
        LanguageTableEntry(language_, state, SubtreeSymbol(cache->token), tableEntry);
        if (CanReuseFirstLeaf(state, cache->token, tableEntry)) {
            SubtreeRetain(cache->token);
            return cache->token;
        }
    }
    return kNullSubtree;
}

void Engine::SetCachedToken(std::uint32_t byteIndex, Subtree lastExternalToken, Subtree token) {
    TokenCache* cache = &tokenCache_;
    if (token.ptr != nullptr)
        SubtreeRetain(token);
    if (lastExternalToken.ptr != nullptr)
        SubtreeRetain(lastExternalToken);
    if (cache->token.ptr != nullptr)
        SubtreeRelease(&treePool_, cache->token);
    if (cache->lastExternalToken.ptr != nullptr)
        SubtreeRelease(&treePool_, cache->lastExternalToken);
    cache->token             = token;
    cache->byteIndex         = byteIndex;
    cache->lastExternalToken = lastExternalToken;
}

Subtree Engine::ReuseNode(StackVersion version, abi::StateId* state, std::uint32_t position, Subtree lastExternalToken,
                          TableEntry* tableEntry) {
    Subtree result;
    while ((result = ReusableNodeTree(&reusableNode_)).ptr != nullptr) {
        const std::uint32_t byteOffset    = ReusableNodeByteOffset(&reusableNode_);
        std::uint32_t       endByteOffset = byteOffset + SubtreeTotalBytes(result);

        // Do not reuse an EOF node if the included ranges array has changes
        // later on in the file.
        if (SubtreeIsEof(result))
            endByteOffset = 0xFFFFFFFF;

        if (byteOffset > position)
            break;

        if (byteOffset < position) {
            if (endByteOffset <= position || !ReusableNodeDescend(&reusableNode_))
                ReusableNodeAdvance(&reusableNode_);
            continue;
        }

        if (!SubtreeExternalScannerState(reusableNode_.lastExternalToken)
                 ->Eq(SubtreeExternalScannerState(lastExternalToken)->Data(),
                      SubtreeExternalScannerState(lastExternalToken)->length)) {
            ReusableNodeAdvance(&reusableNode_);
            continue;
        }

        bool cannotReuse = false;
        if (SubtreeHasChanges(result) || SubtreeIsError(result) || SubtreeMissing(result) || SubtreeIsFragile(result))
            cannotReuse = true;

        if (cannotReuse) {
            if (!ReusableNodeDescend(&reusableNode_)) {
                ReusableNodeAdvance(&reusableNode_);
                BreakdownTopOfStack(version);
                *state = stack_->State(version);
            }
            continue;
        }

        const abi::Symbol leafSymbol = SubtreeLeafSymbol(result);
        LanguageTableEntry(language_, *state, leafSymbol, tableEntry);
        if (!CanReuseFirstLeaf(*state, result, tableEntry)) {
            ReusableNodeAdvancePastLeaf(&reusableNode_);
            break;
        }

        SubtreeRetain(result);
        return result;
    }

    return kNullSubtree;
}

bool Engine::SelectTree(Subtree left, Subtree right) {
    if (left.ptr == nullptr)
        return true;
    if (right.ptr == nullptr)
        return false;

    if (SubtreeErrorCost(right) < SubtreeErrorCost(left))
        return true;
    if (SubtreeErrorCost(left) < SubtreeErrorCost(right))
        return false;

    if (SubtreeDynamicPrecedence(right) > SubtreeDynamicPrecedence(left))
        return true;
    if (SubtreeDynamicPrecedence(left) > SubtreeDynamicPrecedence(right))
        return false;

    if (SubtreeErrorCost(left) > 0)
        return true;

    const int comparison = SubtreeCompare(left, right, &treePool_);
    return comparison == 1;
}

bool Engine::SelectChildren(Subtree left, const SubtreeArray* children) {
    scratchTrees_.Assign(*children);

    // A temporary subtree over the scratch array; never explicitly released,
    // so the scratch buffer is reusable.
    const MutableSubtree scratchTree = SubtreeNewNode(SubtreeSymbol(left), &scratchTrees_, 0, language_);

    return SelectTree(left, SubtreeFromMut(scratchTree));
}

void Engine::Shift(StackVersion version, abi::StateId state, Subtree lookahead, bool extra) {
    const bool isLeaf        = SubtreeChildCount(lookahead) == 0;
    Subtree    subtreeToPush = lookahead;
    if (extra != SubtreeExtra(lookahead) && isLeaf) {
        MutableSubtree result = SubtreeMakeMut(&treePool_, lookahead);
        SubtreeSetExtra(&result, extra);
        subtreeToPush = SubtreeFromMut(result);
    }

    stack_->Push(version, subtreeToPush, !isLeaf, state);
    if (SubtreeHasExternalTokens(subtreeToPush))
        stack_->SetLastExternalToken(version, SubtreeLastExternalToken(subtreeToPush));
}

StackVersion Engine::Reduce(StackVersion version, abi::Symbol symbol, std::uint32_t count, int dynamicPrecedence,
                            std::uint16_t productionId, bool isFragile, bool endOfNonTerminalExtra) {
    const std::uint32_t initialVersionCount = stack_->VersionCount();

    StackSliceArray     pop                 = stack_->PopCount(version, count);
    std::uint32_t       removedVersionCount = 0;
    const std::uint32_t haltedVersionCount  = stack_->HaltedVersionCount();
    for (std::uint32_t i = 0; i < pop.size; i++) {
        StackSlice         slice        = pop[i];
        const StackVersion sliceVersion = slice.version - removedVersionCount;

        // Allow the maximum version count to be temporarily exceeded, but
        // only by a limited threshold.
        if (sliceVersion > kMaxVersionCount + kMaxVersionCountOverflow + haltedVersionCount) {
            stack_->RemoveVersion(sliceVersion);
            SubtreeArrayDelete(&treePool_, &slice.subtrees);
            removedVersionCount++;
            while (i + 1 < pop.size) {
                StackSlice nextSlice = pop[i + 1];
                if (nextSlice.version != slice.version)
                    break;
                SubtreeArrayDelete(&treePool_, &nextSlice.subtrees);
                i++;
            }
            continue;
        }

        // Extra tokens on top of the stack are re-pushed after the parent.
        SubtreeArray children = slice.subtrees;
        SubtreeArrayRemoveTrailingExtras(&children, &trailingExtras_);

        MutableSubtree parent = SubtreeNewNode(symbol, &children, productionId, language_);

        // Multiple stack versions may have collapsed into one; choose one
        // array of children and delete the rest.
        while (i + 1 < pop.size) {
            StackSlice nextSlice = pop[i + 1];
            if (nextSlice.version != slice.version)
                break;
            i++;

            SubtreeArray nextSliceChildren = nextSlice.subtrees;
            SubtreeArrayRemoveTrailingExtras(&nextSliceChildren, &trailingExtras2_);

            if (SelectChildren(SubtreeFromMut(parent), &nextSliceChildren)) {
                SubtreeArrayClear(&treePool_, &trailingExtras_);
                SubtreeRelease(&treePool_, SubtreeFromMut(parent));
                trailingExtras_.Swap(trailingExtras2_);
                parent = SubtreeNewNode(symbol, &nextSliceChildren, productionId, language_);
            }
            else {
                trailingExtras2_.Clear();
                SubtreeArrayDelete(&treePool_, &nextSlice.subtrees);
            }
        }

        const abi::StateId state     = stack_->State(sliceVersion);
        const abi::StateId nextState = LanguageNextState(language_, state, symbol);
        if (endOfNonTerminalExtra && nextState == state)
            parent.ptr->extra = true;
        if (isFragile || pop.size > 1 || initialVersionCount > 1) {
            parent.ptr->fragileLeft  = true;
            parent.ptr->fragileRight = true;
            parent.ptr->parseState   = kTreeStateNone;
        }
        else {
            parent.ptr->parseState = state;
        }
        parent.ptr->dynamicPrecedence += dynamicPrecedence;

        stack_->Push(sliceVersion, SubtreeFromMut(parent), false, nextState);
        for (std::uint32_t j = 0; j < trailingExtras_.size; j++)
            stack_->Push(sliceVersion, trailingExtras_[j], false, nextState);

        for (StackVersion j = 0; j < sliceVersion; j++) {
            if (j == version)
                continue;
            if (stack_->Merge(j, sliceVersion)) {
                removedVersionCount++;
                break;
            }
        }
    }

    return stack_->VersionCount() > initialVersionCount ? initialVersionCount : kStackVersionNone;
}

void Engine::Accept(StackVersion version, Subtree lookahead) {
    stack_->Push(version, lookahead, false, 1);

    StackSliceArray pop = stack_->PopAll(version);
    for (std::uint32_t i = 0; i < pop.size; i++) {
        SubtreeArray trees = pop[i].subtrees;

        Subtree root = kNullSubtree;
        for (std::uint32_t j = trees.size - 1; j + 1 > 0; j--) {
            const Subtree tree = trees[j];
            if (!SubtreeExtra(tree)) {
                const std::uint32_t childCount = SubtreeChildCount(tree);
                const Subtree*      children   = SubtreeChildren(tree);
                for (std::uint32_t k = 0; k < childCount; k++)
                    SubtreeRetain(children[k]);
                trees.Splice(j, 1, childCount, children);
                root = SubtreeFromMut(SubtreeNewNode(SubtreeSymbol(tree), &trees, tree.ptr->productionId, language_));
                SubtreeRelease(&treePool_, tree);
                break;
            }
        }

        acceptCount_++;

        if (finishedTree_.ptr != nullptr) {
            if (SelectTree(finishedTree_, root)) {
                SubtreeRelease(&treePool_, finishedTree_);
                finishedTree_ = root;
            }
            else {
                SubtreeRelease(&treePool_, root);
            }
        }
        else {
            finishedTree_ = root;
        }
    }

    stack_->RemoveVersion(pop[0].version);
    stack_->Halt(version);
}

bool Engine::DoAllPotentialReductions(StackVersion startingVersion, abi::Symbol lookaheadSymbol) {
    const std::uint32_t initialVersionCount = stack_->VersionCount();

    bool         canShiftLookaheadSymbol = false;
    StackVersion version                 = startingVersion;
    for (unsigned i = 0; true; i++) {
        const std::uint32_t versionCount = stack_->VersionCount();
        if (version >= versionCount)
            break;

        bool merged = false;
        for (StackVersion j = initialVersionCount; j < version; j++) {
            if (stack_->Merge(j, version)) {
                merged = true;
                break;
            }
        }
        if (merged)
            continue;

        const abi::StateId state          = stack_->State(version);
        bool               hasShiftAction = false;
        reduceActions_.Clear();

        abi::Symbol firstSymbol = 0;
        abi::Symbol endSymbol   = 0;
        if (lookaheadSymbol != 0) {
            firstSymbol = lookaheadSymbol;
            endSymbol   = lookaheadSymbol + 1;
        }
        else {
            firstSymbol = 1;
            endSymbol   = static_cast<abi::Symbol>(language_->tokenCount);
        }

        for (abi::Symbol symbol = firstSymbol; symbol < endSymbol; symbol++) {
            TableEntry entry;
            LanguageTableEntry(language_, state, symbol, &entry);
            for (std::uint32_t j = 0; j < entry.actionCount; j++) {
                const abi::ParseAction action = entry.actions[j];
                switch (action.type) {
                    case abi::ParseActionTypeShift:
                    case abi::ParseActionTypeRecover:
                        if (!action.shift.extra && !action.shift.repetition)
                            hasShiftAction = true;
                        break;
                    case abi::ParseActionTypeReduce:
                        if (action.reduce.childCount > 0) {
                            const ReduceActionEntry newAction = {
                                .count             = action.reduce.childCount,
                                .symbol            = action.reduce.symbol,
                                .dynamicPrecedence = action.reduce.dynamicPrecedence,
                                .productionId      = action.reduce.productionId,
                            };
                            bool exists = false;
                            for (std::uint32_t k = 0; k < reduceActions_.size; k++) {
                                if (reduceActions_[k].symbol == newAction.symbol && reduceActions_[k].count == newAction.count) {
                                    exists = true;
                                    break;
                                }
                            }
                            if (!exists)
                                reduceActions_.Push(newAction);
                        }
                        break;
                    default:
                        break;
                }
            }
        }

        StackVersion reductionVersion = kStackVersionNone;
        for (std::uint32_t j = 0; j < reduceActions_.size; j++) {
            const ReduceActionEntry action = reduceActions_[j];
            reductionVersion =
                Reduce(version, action.symbol, action.count, action.dynamicPrecedence, action.productionId, true, false);
        }

        if (hasShiftAction) {
            canShiftLookaheadSymbol = true;
        }
        else if (reductionVersion != kStackVersionNone && i < kMaxVersionCount) {
            stack_->RenumberVersion(reductionVersion, version);
            continue;
        }
        else if (lookaheadSymbol != 0) {
            stack_->RemoveVersion(version);
        }

        if (version == startingVersion) {
            version = versionCount;
        }
        else {
            version++;
        }
    }

    return canShiftLookaheadSymbol;
}

bool Engine::RecoverToState(StackVersion version, unsigned depth, abi::StateId goalState) {
    StackSliceArray pop             = stack_->PopCount(version, depth);
    StackVersion    previousVersion = kStackVersionNone;

    for (std::uint32_t i = 0; i < pop.size; i++) {
        StackSlice slice = pop[i];

        if (slice.version == previousVersion) {
            SubtreeArrayDelete(&treePool_, &slice.subtrees);
            pop.Erase(i--);
            continue;
        }

        if (stack_->State(slice.version) != goalState) {
            stack_->Halt(slice.version);
            SubtreeArrayDelete(&treePool_, &slice.subtrees);
            pop.Erase(i--);
            continue;
        }

        SubtreeArray errorTrees = stack_->PopError(slice.version);
        if (errorTrees.size > 0) {
            const Subtree       errorTree       = errorTrees[0];
            const std::uint32_t errorChildCount = SubtreeChildCount(errorTree);
            if (errorChildCount > 0) {
                slice.subtrees.Splice(0, 0, errorChildCount, SubtreeChildren(errorTree));
                for (std::uint32_t j = 0; j < errorChildCount; j++)
                    SubtreeRetain(slice.subtrees[j]);
            }
            SubtreeArrayDelete(&treePool_, &errorTrees);
        }

        SubtreeArrayRemoveTrailingExtras(&slice.subtrees, &trailingExtras_);

        if (slice.subtrees.size > 0) {
            const Subtree error = SubtreeNewErrorNode(&slice.subtrees, true, language_);
            stack_->Push(slice.version, error, false, goalState);
        }
        else {
            slice.subtrees.Delete();
        }

        for (std::uint32_t j = 0; j < trailingExtras_.size; j++) {
            const Subtree tree = trailingExtras_[j];
            stack_->Push(slice.version, tree, false, goalState);
        }

        previousVersion = slice.version;
    }

    return previousVersion != kStackVersionNone;
}

void Engine::Recover(StackVersion version, Subtree lookahead) {
    bool           didRecover           = false;
    const unsigned previousVersionCount = stack_->VersionCount();
    const Length   position             = stack_->Position(version);
    StackSummary*  summary              = stack_->GetSummary(version);
    const unsigned nodeCountSinceError  = stack_->NodeCountSinceError(version);
    const unsigned currentErrorCost     = stack_->ErrorCost(version);

    // Strategy 1: recover to a previous state on the stack in which the
    // lookahead token would be valid.
    if (summary != nullptr && !SubtreeIsError(lookahead)) {
        for (std::uint32_t i = 0; i < summary->size; i++) {
            const StackSummaryEntry entry = (*summary)[i];

            if (entry.state == kErrorState)
                continue;
            if (entry.position.bytes == position.bytes)
                continue;
            unsigned depth = entry.depth;
            if (nodeCountSinceError > 0)
                depth++;

            // Do not recover in ways that create redundant stack versions.
            bool wouldMerge = false;
            for (unsigned j = 0; j < previousVersionCount; j++) {
                if (stack_->State(j) == entry.state && stack_->Position(j).bytes == position.bytes) {
                    wouldMerge = true;
                    break;
                }
            }
            if (wouldMerge)
                continue;

            // Do not recover if the result would clearly be worse than some
            // existing stack version.
            const unsigned newCost = currentErrorCost + entry.depth * kErrorCostPerSkippedTree +
                                     (position.bytes - entry.position.bytes) * kErrorCostPerSkippedChar +
                                     (position.extent.row - entry.position.extent.row) * kErrorCostPerSkippedLine;
            if (BetterVersionExists(version, false, newCost))
                break;

            if (LanguageHasActions(language_, entry.state, SubtreeSymbol(lookahead))) {
                if (RecoverToState(version, depth, entry.state)) {
                    didRecover = true;
                    break;
                }
            }
        }
    }

    // Remove any versions created and subsequently halted during recovery.
    for (unsigned i = previousVersionCount; i < stack_->VersionCount(); i++) {
        if (!stack_->IsActive(i))
            stack_->RemoveVersion(i--);
    }

    // At the end of the file, wrap everything in an ERROR node and terminate.
    if (SubtreeIsEof(lookahead)) {
        SubtreeArray  children{};
        const Subtree parent = SubtreeNewErrorNode(&children, false, language_);
        stack_->Push(version, parent, false, 1);
        Accept(version, lookahead);
        return;
    }

    // Strategy 2: skip the current lookahead token by wrapping it in an
    // ERROR node — unless there are already too many stack versions.
    if (didRecover && stack_->VersionCount() > kMaxVersionCount) {
        stack_->Halt(version);
        SubtreeRelease(&treePool_, lookahead);
        return;
    }

    if (didRecover && SubtreeHasExternalScannerStateChange(lookahead)) {
        stack_->Halt(version);
        SubtreeRelease(&treePool_, lookahead);
        return;
    }

    // Do not recover if the result would clearly be worse.
    const unsigned newCost = currentErrorCost + kErrorCostPerSkippedTree +
                             SubtreeTotalBytes(lookahead) * kErrorCostPerSkippedChar +
                             SubtreeTotalSize(lookahead).extent.row * kErrorCostPerSkippedLine;
    if (BetterVersionExists(version, false, newCost)) {
        stack_->Halt(version);
        SubtreeRelease(&treePool_, lookahead);
        return;
    }

    // If the lookahead is an extra token, mark it as extra so it isn't
    // counted in error cost calculations.
    std::uint32_t           n       = 0;
    const abi::ParseAction* actions = LanguageActions(language_, 1, SubtreeSymbol(lookahead), &n);
    if (n > 0 && actions[n - 1].type == abi::ParseActionTypeShift && actions[n - 1].shift.extra) {
        MutableSubtree mutableLookahead = SubtreeMakeMut(&treePool_, lookahead);
        SubtreeSetExtra(&mutableLookahead, true);
        lookahead = SubtreeFromMut(mutableLookahead);
    }

    // Wrap the lookahead token in an ERROR.
    SubtreeArray children{};
    children.Reserve(1);
    children.Push(lookahead);
    MutableSubtree errorRepeat = SubtreeNewNode(abi::kBuiltinSymbolErrorRepeat, &children, 0, language_);

    // If there is already an ERROR at the top of the stack, wrap the two
    // ERRORs together into one larger ERROR.
    if (nodeCountSinceError > 0) {
        StackSliceArray pop = stack_->PopCount(version, 1);

        // If multiple stack versions have merged, pick one arbitrarily.
        if (pop.size > 1) {
            for (std::uint32_t i = 1; i < pop.size; i++)
                SubtreeArrayDelete(&treePool_, &pop[i].subtrees);
            while (stack_->VersionCount() > pop[0].version + 1)
                stack_->RemoveVersion(pop[0].version + 1);
        }

        stack_->RenumberVersion(pop[0].version, version);
        pop[0].subtrees.Push(SubtreeFromMut(errorRepeat));
        errorRepeat = SubtreeNewNode(abi::kBuiltinSymbolErrorRepeat, &pop[0].subtrees, 0, language_);
    }

    stack_->Push(version, SubtreeFromMut(errorRepeat), false, kErrorState);
    if (SubtreeHasExternalTokens(lookahead))
        stack_->SetLastExternalToken(version, SubtreeLastExternalToken(lookahead));
}

// Reduces everything `version` can reduce without a lookahead, and returns
// the stack depth that leaves it at.
//
// "Regardless of lookahead" can leave the continuation worth keeping in a
// version DoAllPotentialReductions forked off rather than in the one passed
// in: a state with both shift and reduce actions keeps its unreduced self
// and puts each reduction in a new version. The shallowest of that family
// is the most-closed reading of the same input, which is the one the EOF
// pass is after, so this collapses the family back onto `version`.
//
// `version` must be the newest one on the stack -- every caller here has
// just copied it -- so that the family is exactly the versions above it.
unsigned Engine::SettleReductions(StackVersion version) {
    DoAllPotentialReductions(version, 0);

    StackVersion shallowest = version;
    unsigned     bestDepth  = stack_->Depth(version);
    for (StackVersion candidate = version + 1; candidate < stack_->VersionCount(); candidate++) {
        const unsigned candidateDepth = stack_->Depth(candidate);
        if (candidateDepth < bestDepth) {
            bestDepth  = candidateDepth;
            shallowest = candidate;
        }
    }

    if (shallowest != version)
        stack_->RenumberVersion(shallowest, version);
    while (stack_->VersionCount() > version + 1)
        stack_->RemoveVersion(stack_->VersionCount() - 1);

    return bestDepth;
}

// Whether `version` could consume the end token from where it stands --
// after every reduction that token licenses, is it actually accepted?
//
// The weaker question ("does this state have any action for the end token")
// is not the same thing and cannot stand in for it: a reduce action on the
// end token only means the stack can fold once more, and folding a
// construct the repair itself invented leaves the parse exactly where it
// was. That is what lets a closer that costs its own depth back still count
// ("};" finishing a C++ class shifts the ";" on before the declaration it
// completes reduces away) without also blessing one that closes nothing.
//
// Asked on a copy: the reductions here are how the question is answered,
// not a move the parse gets to keep.
bool Engine::ParseCanFinish(StackVersion version, abi::Symbol endSymbol) {
    const StackVersion probe     = stack_->CopyVersion(version);
    bool               canFinish = false;

    for (unsigned step = 0; step < kMaxFinishReductions && !canFinish; step++) {
        TableEntry entry;
        LanguageTableEntry(language_, stack_->State(probe), endSymbol, &entry);

        bool reduced = false;
        for (std::uint32_t i = 0; i < entry.actionCount && !canFinish && !reduced; i++) {
            switch (entry.actions[i].type) {
                case abi::ParseActionTypeAccept:
                case abi::ParseActionTypeShift:
                    canFinish = true;
                    break;

                case abi::ParseActionTypeReduce: {
                    const abi::ParseAction action = entry.actions[i];
                    const StackVersion     reductionVersion =
                        Reduce(probe, action.reduce.symbol, action.reduce.childCount, action.reduce.dynamicPrecedence,
                               action.reduce.productionId, false, false);
                    if (reductionVersion == kStackVersionNone)
                        break;
                    stack_->RenumberVersion(reductionVersion, probe);
                    reduced = true;
                    break;
                }

                default:
                    break;
            }
        }

        if (!reduced)
            break;
    }

    while (stack_->VersionCount() > version + 1)
        stack_->RemoveVersion(stack_->VersionCount() - 1);
    return canFinish;
}

// The largest production any reduction in `state` completes -- zero if it
// has none. Memoized: the candidate lists below ask this of the same handful
// of states repeatedly, and it is a pure function of the tables.
std::uint32_t Engine::LargestReductionIn(abi::StateId state) {
    const auto cached = stateReductionSizes_.find(state);
    if (cached != stateReductionSizes_.end())
        return cached->second;

    std::uint32_t largest = 0;
    for (abi::Symbol lookaheadSymbol = 1; lookaheadSymbol < static_cast<abi::Symbol>(language_->tokenCount);
         lookaheadSymbol++) {
        TableEntry entry;
        LanguageTableEntry(language_, state, lookaheadSymbol, &entry);
        for (std::uint32_t i = 0; i < entry.actionCount; i++) {
            if (entry.actions[i].type == abi::ParseActionTypeReduce)
                largest = std::max(largest, static_cast<std::uint32_t>(entry.actions[i].reduce.childCount));
        }
    }

    stateReductionSizes_.emplace(state, largest);
    return largest;
}

// The terminals worth trying as a closer from `state`, in the order to try
// them.
//
// Order is what makes the search below cheap and its answers stable, since
// that search stops at the first candidate that proves itself: a C++ state
// inside two open braces offers 133 of them, and only "}" closes anything.
// Three things decide it, in order:
//
//  - a terminal that reduces straight onto the end token is one
//    HandleError's own single-token search would already have found and
//    taken, so those come first, in its order (ascending symbol) -- a
//    document ordinary recovery could repair is then repaired identically;
//  - then by the largest production the terminal stands to complete, which
//    puts a block's "}" ahead of a statement's ";";
//  - ascending symbol otherwise, so the choice never depends on table
//    layout.
//
// Terminals that reduce nothing at all stay in the list, at the back: they
// are no way to START a closer, but a closer spelled in several tokens
// needs them in the middle ("</" reduces nothing until a name and a ">"
// follow it). The one real exclusion is an extra -- a comment, XML's
// CharData -- which shifts without leaving the state, so inserting one
// spells nothing and only invites the search to try it again from the same
// place.
//
// All of it is a hint: CloseInnermostConstruct proves every insertion
// against the stack before keeping it.
const std::vector<abi::Symbol>& Engine::EofClosingCandidates(abi::StateId state) {
    const auto cached = eofClosers_.find(state);
    if (cached != eofClosers_.end())
        return cached->second;

    struct Ranked {
        bool          repairsInPlace; // what HandleError's own one-token search would take
        std::uint32_t weight;         // largest production it stands to complete
        abi::Symbol   symbol;
    };

    std::vector<Ranked> ranked;
    for (abi::Symbol symbol = 1; symbol < static_cast<abi::Symbol>(language_->tokenCount); symbol++) {
        TableEntry entry;
        LanguageTableEntry(language_, state, symbol, &entry);
        if (entry.actionCount == 0)
            continue;

        std::uint32_t weight = 0;
        for (std::uint32_t i = 0; i < entry.actionCount; i++) {
            if (entry.actions[i].type == abi::ParseActionTypeReduce)
                weight = std::max(weight, static_cast<std::uint32_t>(entry.actions[i].reduce.childCount));
        }

        const abi::StateId stateAfterSymbol = LanguageNextState(language_, state, symbol);
        if (stateAfterSymbol == state && weight == 0)
            continue;

        bool repairsInPlace = false;
        if (stateAfterSymbol != 0 && stateAfterSymbol != state) {
            weight         = std::max(weight, LargestReductionIn(stateAfterSymbol));
            repairsInPlace = LanguageHasReduceAction(language_, stateAfterSymbol, abi::kBuiltinSymbolEnd);
        }

        ranked.push_back({.repairsInPlace = repairsInPlace, .weight = weight, .symbol = symbol});
    }

    std::sort(ranked.begin(), ranked.end(), [](const Ranked& left, const Ranked& right) {
        if (left.repairsInPlace != right.repairsInPlace)
            return left.repairsInPlace;
        if (!left.repairsInPlace && left.weight != right.weight)
            return left.weight > right.weight;
        return left.symbol < right.symbol;
    });

    std::vector<abi::Symbol> candidates;
    candidates.reserve(ranked.size());
    for (const Ranked& entry : ranked)
        candidates.push_back(entry.symbol);

    return eofClosers_.emplace(state, std::move(candidates)).first->second;
}

// Spells out one closer for the construct `version` sits inside, as up to
// `tokenBudget` MISSING terminals, and applies it to `version` on success.
//
// A closer earns its insertion by leaving the stack shallower than
// `depthToBeat` -- it closed a construct -- or by leaving the document
// finished (ParseCanFinish), which is how a closer that costs its own depth
// back still counts. Neither test can be fooled by a token that merely
// continues a construct, and neither is a heuristic: both are read off the
// stack the insertion actually produced.
//
// Most closers are one token. The budget exists for the ones that are not:
// XML and JSX end an element with "</", its name and ">", and only the last
// of the three pays anything back, so they are unreachable one token at a
// time. Tried depth-first over the ranked candidates, widest-first by the
// caller's iterative deepening, so the common single-token case costs one
// trial and the rest stay rare.
//
// `version` must be the newest one on the stack; each trial copies it, and
// a failed trial is removed, so the stack is left exactly as it was found.
unsigned Engine::CloseInnermostConstruct(StackVersion version, abi::Symbol endSymbol, unsigned depthToBeat,
                                         unsigned tokenBudget, bool firstToken, Length padding,
                                         std::uint32_t lookaheadBytes) {
    const abi::StateId state = stack_->State(version);
    const unsigned     depth = stack_->Depth(version);

    const unsigned branchLimit = firstToken ? kMaxCloserBranch : kMaxCloserBranchDeeper;
    unsigned       branched    = 0;

    for (const abi::Symbol symbol : EofClosingCandidates(state)) {
        if (branched >= branchLimit)
            break;

        const StackVersion trial = stack_->CopyVersion(version);

        // Take the token the way the parse loop would: the reductions its
        // own lookahead licenses first, then the shift.
        if (!DoAllPotentialReductions(trial, symbol)) {
            while (stack_->VersionCount() > version + 1)
                stack_->RemoveVersion(stack_->VersionCount() - 1);
            continue;
        }

        const abi::StateId shiftState = LanguageNextState(language_, stack_->State(trial), symbol);
        if (shiftState == 0) {
            while (stack_->VersionCount() > version + 1)
                stack_->RemoveVersion(stack_->VersionCount() - 1);
            continue;
        }

        stack_->Push(trial, SubtreeNewMissingLeaf(&treePool_, symbol, padding, lookaheadBytes, language_), false,
                     shiftState);
        const unsigned trialDepth = SettleReductions(trial);
        const bool     closed     = trialDepth < depthToBeat || ParseCanFinish(trial, endSymbol);

        // A token that leaves the parser exactly where it started -- an
        // extra, or a repetition that reduces straight back -- has spelled
        // nothing, so it neither closes anything nor deserves a place in the
        // branch budget the real candidates are competing for.
        const bool progressed = stack_->State(trial) != state || trialDepth != depth;

        const unsigned spelledRest =
            closed || !progressed || tokenBudget <= 1
                ? 0
                : CloseInnermostConstruct(trial, endSymbol, depthToBeat, tokenBudget - 1, false, padding, lookaheadBytes);

        if (closed || spelledRest > 0) {
            stack_->RenumberVersion(trial, version);
            return 1 + spelledRest;
        }

        if (progressed)
            branched++;

        while (stack_->VersionCount() > version + 1)
            stack_->RemoveVersion(stack_->VersionCount() - 1);
    }

    return 0;
}

// Closes open constructs on one reading of the stack until the document is
// finished, and reports how many tokens that took (zero if it could not
// finish, in which case the caller drops this reading whole).
unsigned Engine::CloseEveryOpenConstruct(StackVersion version, abi::Symbol endSymbol, Length padding,
                                         std::uint32_t lookaheadBytes) {
    unsigned spelled = 0;

    for (unsigned closure = 0; closure < kMaxEofClosures; closure++) {
        if (ParseCanFinish(version, endSymbol))
            break;

        const unsigned depth = stack_->Depth(version);

        unsigned closerTokens = 0;
        for (unsigned budget = 1; budget <= kMaxCloserTokens && closerTokens == 0; budget++)
            closerTokens = CloseInnermostConstruct(version, endSymbol, depth, budget, true, padding, lookaheadBytes);

        if (closerTokens == 0)
            break;
        spelled += closerTokens;
        eofClosuresApplied_++;
    }

    return spelled > 0 && ParseCanFinish(version, endSymbol) ? spelled : 0;
}

// Closes the constructs a document leaves open at its end, by inserting the
// tokens it still owes as MISSING ones.
//
// A file being edited is nearly always unfinished at EOF -- the braces,
// tags or blocks above the cursor have no closing token yet. HandleError's
// own missing-token search only ever repairs the INNERMOST one, because its
// test ("does inserting this token let the parse reduce with the CURRENT
// lookahead") can only pass at the outermost nesting level: the end token
// is not a valid lookahead inside a block, so the tables carry no action
// for it there. Everything left unreduced was then wrapped in one flat
// ERROR by Recover -- which is what cost every consumer that reads nesting
// the structure it reads. Indent felt it worst: a file with two open blocks
// answered column 0 on every line, and reindenting it stripped the
// indentation it already had.
//
// Runs on copies throughout, so a pass that cannot finish the job leaves
// the stack exactly as it found it and ordinary recovery still gets its
// turn. EOF only: mid-file recovery already repairs well, one error at a
// time.
bool Engine::CloseOpenConstructsAtEof(StackVersion version, Subtree lookahead) {
    if (eofClosuresApplied_ >= kMaxEofClosuresPerParse)
        return false;

    const abi::Symbol endSymbol = SubtreeLeafSymbol(lookahead);

    // The missing tokens' padding positions them within the next included
    // range -- relevant only under ranged parsing, zero otherwise -- exactly
    // as HandleError's own insertion computes it.
    const Length position = stack_->Position(version);
    lexer_.Reset(position);
    lexer_.MarkEnd();
    const Length        padding        = LengthSub(lexer_.tokenEndPosition, position);
    const std::uint32_t lookaheadBytes = SubtreeTotalBytes(lookahead) + SubtreeLookaheadBytes(lookahead);

    // Settling the stack without a lookahead does not yield one reading but
    // a family of them -- a state with both shift and reduce actions keeps
    // its unreduced self and puts each reduction in a version of its own --
    // and which member the closer is reachable from is not knowable up
    // front. Crystal's unterminated string is reachable only from the
    // reduced member, C++'s unclosed blocks only from the unreduced one, and
    // the two are the same depth. So every member gets its own attempt, in
    // the order HandleError's own single-token search would have taken them.
    const StackVersion familyBegin = stack_->VersionCount();
    const StackVersion working     = stack_->CopyVersion(version);
    DoAllPotentialReductions(working, 0);
    const std::uint32_t familyEnd = stack_->VersionCount();

    // Fewest inserted tokens wins, not first found: a document is owed one
    // specific set of tokens, and a repair that spells out more than that is
    // inventing something the file never had. Rust's "({ ... })" is owed a
    // ";", and a member that would rather read the parentheses as a call's
    // argument list and hand it a "(" and a ")" as well must not outrank it
    // just for coming first in the family.
    StackVersion best        = kStackVersionNone;
    unsigned     bestSpelled = 0;
    for (StackVersion member = familyBegin; member < familyEnd; member++) {
        const StackVersion attempt = stack_->CopyVersion(member);
        const unsigned     spelled = CloseEveryOpenConstruct(attempt, endSymbol, padding, lookaheadBytes);

        if (spelled > 0 && (best == kStackVersionNone || spelled < bestSpelled)) {
            const StackVersion previousBest = best;
            best                            = attempt;
            bestSpelled                     = spelled;
            if (previousBest != kStackVersionNone) {
                // Every attempt is copied onto the top, so dropping an
                // earlier one shifts this one down into its place.
                stack_->RemoveVersion(previousBest);
                best--;
            }
            continue;
        }

        stack_->RemoveVersion(attempt);
    }

    if (best != kStackVersionNone) {
        stack_->RenumberVersion(best, version);
        while (stack_->VersionCount() > familyBegin)
            stack_->RemoveVersion(stack_->VersionCount() - 1);
        return true;
    }

    while (stack_->VersionCount() > familyBegin)
        stack_->RemoveVersion(stack_->VersionCount() - 1);
    return false;
}

void Engine::HandleError(StackVersion version, Subtree lookahead) {
    const std::uint32_t previousVersionCount = stack_->VersionCount();

    // Perform any reductions that can happen in this state, regardless of
    // the lookahead.
    DoAllPotentialReductions(version, 0);
    const std::uint32_t versionCount = stack_->VersionCount();
    const Length        position     = stack_->Position(version);

    // Push a discontinuity onto the stack; try inserting one missing token.
    bool didInsertMissingToken = false;
    for (StackVersion v = version; v < versionCount;) {
        if (!didInsertMissingToken) {
            const abi::StateId state = stack_->State(v);
            for (abi::Symbol missingSymbol = 1; missingSymbol < static_cast<std::uint16_t>(language_->tokenCount);
                 missingSymbol++) {
                const abi::StateId stateAfterMissingSymbol = LanguageNextState(language_, state, missingSymbol);
                if (stateAfterMissingSymbol == 0 || stateAfterMissingSymbol == state)
                    continue;

                if (LanguageHasReduceAction(language_, stateAfterMissingSymbol, SubtreeLeafSymbol(lookahead))) {
                    // The missing token's padding positions it within the
                    // next included range (relevant only under ranged
                    // parsing; padding is zero here otherwise).
                    lexer_.Reset(position);
                    lexer_.MarkEnd();
                    const Length        padding        = LengthSub(lexer_.tokenEndPosition, position);
                    const std::uint32_t lookaheadBytes = SubtreeTotalBytes(lookahead) + SubtreeLookaheadBytes(lookahead);

                    const StackVersion versionWithMissingTree = stack_->CopyVersion(v);
                    const Subtree      missingTree =
                        SubtreeNewMissingLeaf(&treePool_, missingSymbol, padding, lookaheadBytes, language_);
                    stack_->Push(versionWithMissingTree, missingTree, false, stateAfterMissingSymbol);

                    if (DoAllPotentialReductions(versionWithMissingTree, SubtreeLeafSymbol(lookahead))) {
                        didInsertMissingToken = true;
                        break;
                    }
                }
            }
        }

        stack_->Push(v, kNullSubtree, false, kErrorState);
        v = (v == version) ? previousVersionCount : v + 1;
    }

    for (std::uint32_t i = previousVersionCount; i < versionCount; i++)
        stack_->Merge(version, previousVersionCount);

    stack_->RecordSummary(version, kMaxSummaryDepth);

    // Begin recovery with the current lookahead node right away.
    if (SubtreeChildCount(lookahead) > 0)
        BreakdownLookahead(&lookahead, kErrorState, &reusableNode_);
    Recover(version, lookahead);
}

bool Engine::Advance(StackVersion version, bool allowNodeReuse) {
    abi::StateId        state             = stack_->State(version);
    const std::uint32_t position          = stack_->Position(version).bytes;
    const Subtree       lastExternalToken = stack_->LastExternalToken(version);

    bool       didReuse   = true;
    Subtree    lookahead  = kNullSubtree;
    TableEntry tableEntry = {.actions = nullptr, .actionCount = 0, .isReusable = false};

    // If possible, reuse a node from the previous syntax tree.
    if (allowNodeReuse)
        lookahead = ReuseNode(version, &state, position, lastExternalToken, &tableEntry);

    if (lookahead.ptr == nullptr) {
        didReuse  = false;
        lookahead = GetCachedToken(state, position, lastExternalToken, &tableEntry);
    }

    bool needsLex = lookahead.ptr == nullptr;
    for (;;) {
        if (needsLex) {
            needsLex  = false;
            lookahead = LexToken(version, state);

            if (lookahead.ptr != nullptr) {
                SetCachedToken(position, lastExternalToken, lookahead);
                LanguageTableEntry(language_, state, SubtreeSymbol(lookahead), &tableEntry);
            }
            else {
                // Parsing a non-terminal extra: a null lookahead means the
                // end of the rule; the reduction lives in the EOF entry.
                LanguageTableEntry(language_, state, abi::kBuiltinSymbolEnd, &tableEntry);
            }
        }

        bool         didReduce            = false;
        StackVersion lastReductionVersion = kStackVersionNone;
        for (std::uint32_t i = 0; i < tableEntry.actionCount; i++) {
            const abi::ParseAction action = tableEntry.actions[i];

            switch (action.type) {
                case abi::ParseActionTypeShift: {
                    if (action.shift.repetition)
                        break;
                    abi::StateId nextState = 0;
                    if (action.shift.extra) {
                        nextState = state;
                    }
                    else {
                        nextState = action.shift.state;
                    }

                    if (SubtreeChildCount(lookahead) > 0) {
                        BreakdownLookahead(&lookahead, state, &reusableNode_);
                        nextState = LanguageNextState(language_, state, SubtreeSymbol(lookahead));
                    }

                    Shift(version, nextState, lookahead, action.shift.extra);
                    if (didReuse)
                        ReusableNodeAdvance(&reusableNode_);
                    return true;
                }

                case abi::ParseActionTypeReduce: {
                    const bool         isFragile             = tableEntry.actionCount > 1;
                    const bool         endOfNonTerminalExtra = lookahead.ptr == nullptr;
                    const StackVersion reductionVersion =
                        Reduce(version, action.reduce.symbol, action.reduce.childCount, action.reduce.dynamicPrecedence,
                               action.reduce.productionId, isFragile, endOfNonTerminalExtra);
                    didReduce = true;
                    if (reductionVersion != kStackVersionNone)
                        lastReductionVersion = reductionVersion;
                    break;
                }

                case abi::ParseActionTypeAccept: {
                    Accept(version, lookahead);
                    return true;
                }

                case abi::ParseActionTypeRecover: {
                    if (SubtreeChildCount(lookahead) > 0)
                        BreakdownLookahead(&lookahead, kErrorState, &reusableNode_);

                    Recover(version, lookahead);
                    if (didReuse)
                        ReusableNodeAdvance(&reusableNode_);
                    return true;
                }

                default:
                    break;
            }
        }

        if (lastReductionVersion != kStackVersionNone) {
            stack_->RenumberVersion(lastReductionVersion, version);
            state = stack_->State(version);

            if (lookahead.ptr == nullptr) {
                needsLex = true;
            }
            else {
                LanguageTableEntry(language_, state, SubtreeLeafSymbol(lookahead), &tableEntry);
            }

            continue;
        }

        // A reduction was performed but merged into an existing version.
        if (didReduce) {
            if (lookahead.ptr != nullptr)
                SubtreeRelease(&treePool_, lookahead);
            stack_->Halt(version);
            return true;
        }

        // An invalid keyword may still be valid as the default word token.
        if (SubtreeIsKeyword(lookahead) && SubtreeSymbol(lookahead) != language_->keywordCaptureToken &&
            !LanguageIsReservedWord(language_, state, SubtreeSymbol(lookahead))) {
            LanguageTableEntry(language_, state, language_->keywordCaptureToken, &tableEntry);
            if (tableEntry.actionCount > 0) {
                MutableSubtree mutableLookahead = SubtreeMakeMut(&treePool_, lookahead);
                SubtreeSetSymbol(&mutableLookahead, language_->keywordCaptureToken, language_);
                lookahead = SubtreeFromMut(mutableLookahead);
                continue;
            }
        }

        // A reused subtree turned out to be invalid: break it down.
        if (BreakdownTopOfStack(version)) {
            state = stack_->State(version);
            SubtreeRelease(&treePool_, lookahead);
            needsLex = true;
            continue;
        }

        // At EOF, first try to close whatever the document left open rather
        // than handing the whole stack to error recovery -- see
        // CloseOpenConstructsAtEof. On success the state can act on the end
        // token again and the ordinary reduce/accept path below takes over.
        if (SubtreeIsEof(lookahead) && CloseOpenConstructsAtEof(version, lookahead)) {
            state = stack_->State(version);
            LanguageTableEntry(language_, state, SubtreeLeafSymbol(lookahead), &tableEntry);
            continue;
        }

        // A real error: pause this version; recovery starts if every
        // version ends up paused.
        stack_->Pause(version, lookahead);
        return true;
    }
}

unsigned Engine::CondenseStack() {
    unsigned minErrorCost = 0xFFFFFFFF;
    for (StackVersion i = 0; i < stack_->VersionCount(); i++) {
        if (stack_->IsHalted(i)) {
            stack_->RemoveVersion(i);
            i--;
            continue;
        }

        const ErrorStatus statusI = VersionStatus(i);
        if (!statusI.isInError && statusI.cost < minErrorCost)
            minErrorCost = statusI.cost;

        for (StackVersion j = 0; j < i; j++) {
            const ErrorStatus statusJ = VersionStatus(j);

            switch (CompareVersions(statusJ, statusI)) {
                case ErrorComparison::TakeLeft:
                    stack_->RemoveVersion(i);
                    i--;
                    j = i;
                    break;

                case ErrorComparison::PreferLeft:
                case ErrorComparison::None:
                    if (stack_->Merge(j, i)) {
                        i--;
                        j = i;
                    }
                    break;

                case ErrorComparison::PreferRight:
                    if (stack_->Merge(j, i)) {
                        i--;
                        j = i;
                    }
                    else {
                        stack_->SwapVersions(i, j);
                    }
                    break;

                case ErrorComparison::TakeRight:
                    stack_->RemoveVersion(j);
                    i--;
                    j--;
                    break;
            }
        }
    }

    while (stack_->VersionCount() > kMaxVersionCount)
        stack_->RemoveVersion(kMaxVersionCount);

    if (stack_->VersionCount() > 0) {
        bool hasUnpausedVersion = false;
        for (StackVersion i = 0, n = stack_->VersionCount(); i < n; i++) {
            if (stack_->IsPaused(i)) {
                if (!hasUnpausedVersion && acceptCount_ < kMaxVersionCount) {
                    minErrorCost            = stack_->ErrorCost(i);
                    const Subtree lookahead = stack_->Resume(i);
                    HandleError(i, lookahead);
                    hasUnpausedVersion = true;
                }
                else {
                    stack_->RemoveVersion(i);
                    i--;
                    n--;
                }
            }
            else {
                hasUnpausedVersion = true;
            }
        }
    }

    return minErrorCost;
}

void Engine::BalanceSubtree() {
    const Subtree finishedTree = finishedTree_;

    treePool_.treeStack.Clear();
    if (SubtreeChildCount(finishedTree) > 0 && finishedTree.ptr->refCount == 1)
        treePool_.treeStack.Push(SubtreeToMutUnsafe(finishedTree));

    while (treePool_.treeStack.size > 0) {
        const MutableSubtree tree = treePool_.treeStack.Back();

        if (tree.ptr->repeatDepth > 0) {
            const Subtree child1      = SubtreeChildren(tree)[0];
            const Subtree child2      = SubtreeChildren(tree)[tree.ptr->childCount - 1];
            const long    repeatDelta = static_cast<long>(SubtreeRepeatDepth(child1)) - static_cast<long>(SubtreeRepeatDepth(child2));
            if (repeatDelta > 0) {
                unsigned n = static_cast<unsigned>(repeatDelta);
                for (unsigned i = n / 2; i > 0; i /= 2) {
                    SubtreeCompress(tree, i, language_, &treePool_.treeStack);
                    n -= i;
                }
            }
        }

        (void)treePool_.treeStack.Pop();

        for (std::uint32_t i = 0; i < tree.ptr->childCount; i++) {
            const Subtree child = SubtreeChildren(tree)[i];
            if (SubtreeChildCount(child) > 0 && child.ptr->refCount == 1)
                treePool_.treeStack.Push(SubtreeToMutUnsafe(child));
        }
    }
}

GreenTree Engine::Parse(std::string_view text) {
    return Parse(text, GreenTree{});
}

GreenTree Engine::Parse(std::string_view text, const GreenTree& oldTree) {
    lexer_.SetText(text);
    eofClosuresApplied_ = 0;

    ExternalScannerCreate();
    ReusableNodeClear(&reusableNode_);
    if (!oldTree.IsNull()) {
        SubtreeRetain(oldTree.Root());
        oldTree_ = oldTree.Root();
        // reusable_node_reset: never reuse the root itself (its structure is
        // non-standard after the EOF/extras transformations of acceptance).
        reusableNode_.stack.Push(ReusableNodeEntry{.tree = oldTree_, .childIndex = 0, .byteOffset = 0});
        if (!ReusableNodeDescend(&reusableNode_))
            ReusableNodeClear(&reusableNode_);
    }

    std::uint32_t position     = 0;
    std::uint32_t lastPosition = 0;
    std::uint32_t versionCount = 0;
    do {
        for (StackVersion version = 0; versionCount = stack_->VersionCount(), version < versionCount; version++) {
            const bool allowNodeReuse = versionCount == 1;
            while (stack_->IsActive(version)) {
                if (!Advance(version, allowNodeReuse)) {
                    Reset();
                    return {};
                }

                position = stack_->Position(version).bytes;
                if (position > lastPosition || (version > 0 && position == lastPosition)) {
                    lastPosition = position;
                    break;
                }
            }
        }

        const unsigned minErrorCost = CondenseStack();

        if (finishedTree_.ptr != nullptr && SubtreeErrorCost(finishedTree_) < minErrorCost) {
            stack_->Clear();
            break;
        }
    }
    while (versionCount != 0);

    BalanceSubtree();

    const Subtree finished = finishedTree_;
    finishedTree_          = kNullSubtree;
    // Retain before Reset so the tree survives the parser's own cleanup.
    GreenTree result(finished, language_);

    Reset();
    return result;
}

} // namespace ned::editor::parse
