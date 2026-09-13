#pragma once

#include <cstdint>

#include "Editor/Parse/Tree.h"

// The red node — the port of tree-sitter's TSNode (node.c): a transient
// value addressing one position in a green tree. context = {startByte,
// startRow, startColumn, aliasSymbol}; id points at the Subtree slot in the
// parent's child array (stable for the tree's lifetime, upstream's own
// node-identity rule). Must not outlive the GreenTree it came from.

namespace ned::editor::parse {

struct RedNode {
    std::uint32_t   context[4];
    const Subtree*  id;
    const TreeData* tree;
};

RedNode NodeNew(const TreeData* tree, const Subtree* subtree, Length position, abi::Symbol alias);
RedNode NodeNull();

bool          NodeIsNull(RedNode self);
std::uint32_t NodeStartByte(RedNode self);
std::uint32_t NodeEndByte(RedNode self);
abi::Point    NodeStartPoint(RedNode self);
abi::Point    NodeEndPoint(RedNode self);
abi::Symbol   NodeSymbol(RedNode self);
const char*   NodeType(RedNode self);
bool          NodeIsNamed(RedNode self);
bool          NodeIsExtra(RedNode self);
bool          NodeIsMissing(RedNode self);
bool          NodeIsError(RedNode self);
bool          NodeHasError(RedNode self);
bool          NodeHasExternalTokens(RedNode self);
bool          NodeEq(RedNode self, RedNode other);
std::uint32_t NodeChildCount(RedNode self);
std::uint32_t NodeNamedChildCount(RedNode self);
RedNode       NodeChild(RedNode self, std::uint32_t childIndex);
RedNode       NodeNamedChild(RedNode self, std::uint32_t childIndex);
RedNode       NodeChildByFieldId(RedNode self, abi::FieldId fieldId);
RedNode       NodeChildByFieldName(RedNode self, const char* name, std::uint32_t nameLength);
RedNode       NodeParent(RedNode self);
RedNode       NodeChildWithDescendant(RedNode self, RedNode descendant);
RedNode       NodeNextSibling(RedNode self);
RedNode       NodeNextNamedSibling(RedNode self);
RedNode       NodePrevSibling(RedNode self);
RedNode       NodePrevNamedSibling(RedNode self);
RedNode       NodeDescendantForByteRange(RedNode self, std::uint32_t start, std::uint32_t end);
RedNode       NodeNamedDescendantForByteRange(RedNode self, std::uint32_t start, std::uint32_t end);

// per-subtree-fact-memoization follow-up: identity that survives a reparse
// when this node's subtree is REUSED, unlike `id` above (a slot address in
// the parent's child array, stable only for one tree's lifetime -- a reused
// subtree gets a new slot address in the new tree even though it's the same
// underlying allocation). This is the underlying heap object's own address
// (Green.h's `SubtreeHeapData*`), retained and re-linked rather than
// recreated whenever incremental reparse reuses a subtree unchanged.
// nullptr for an inline leaf (a small token has no heap allocation to be
// stable at all -- always cheap enough to just re-derive) and for a null
// node. Two nodes with the same non-null identity, from trees produced by
// successive calls to the same IncrementalParseCache, are the SAME
// subtree -- any fact derived from one is valid for the other without
// re-deriving it, unless the pattern that derived it reads outside the
// subtree (see QueryPredicates.h's PredicateReadsOutsideSubtree).
[[nodiscard]] const void* NodeSubtreeIdentity(RedNode self);

} // namespace ned::editor::parse
