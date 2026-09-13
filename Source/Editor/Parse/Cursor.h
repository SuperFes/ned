#pragma once

#include "Editor/Parse/Node.h"
#include "Editor/Parse/RawArray.h"

// The tree cursor — the port of tree-sitter's tree_cursor.c: an explicit
// descent stack over the green tree giving efficient sibling/child walks
// (Node::Child is O(subtree) per call; a cursor walk is amortized O(1))
// plus field-id resolution through hidden wrapper nodes. Same lifetime rule
// as RedNode: must not outlive the tree.

namespace ned::editor::parse {

struct TreeCursorEntry {
    const Subtree* subtree;
    Length         position;
    std::uint32_t  childIndex;
    std::uint32_t  structuralChildIndex;
};

class TreeCursor {
  public:
    explicit TreeCursor(RedNode node) {
        Reset(node);
    }
    ~TreeCursor() {
        stack_.Delete();
    }
    TreeCursor(const TreeCursor&)            = delete;
    TreeCursor& operator=(const TreeCursor&) = delete;

    void                       Reset(RedNode node);
    bool                       GotoFirstChild();
    bool                       GotoNextSibling();
    bool                       GotoParent();
    [[nodiscard]] RedNode      CurrentNode() const;
    [[nodiscard]] abi::FieldId CurrentFieldId() const;

  private:
    enum class Step : std::uint8_t { None,
                                     Hidden,
                                     Visible };

    [[nodiscard]] bool IsEntryVisible(std::uint32_t index) const;
    Step               GotoFirstChildInternal();
    Step               GotoNextSiblingInternal();

    const TreeData*           tree_            = nullptr;
    abi::Symbol               rootAliasSymbol_ = 0;
    RawArray<TreeCursorEntry> stack_;
};

} // namespace ned::editor::parse
