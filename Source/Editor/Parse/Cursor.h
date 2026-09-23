#pragma once

#include <cstdlib>
#include <cstring>

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

// The cursor's descent stack. A RawArray would be a heap allocation per
// cursor, and a cursor is constructed per child walk (Node::ForEachChild,
// QueryMatcher's CollectChildren) as well as per subtree walk -- measured as
// real per-keystroke cost once the walks themselves stopped dominating. The
// inline capacity covers ordinary nesting depth; anything deeper spills to
// the heap and keeps working.
class CursorStack {
  public:
    ~CursorStack() {
        Delete();
    }
    CursorStack()                              = default;
    CursorStack(const CursorStack&)            = delete;
    CursorStack& operator=(const CursorStack&) = delete;

    void Delete() {
        if (contents_ != inline_) {
            std::free(contents_);
        }
        contents_ = inline_;
        capacity_ = kInlineCapacity;
        size      = 0;
    }
    void Clear() {
        size = 0;
    }
    void Push(TreeCursorEntry entry) {
        if (size == capacity_) {
            Grow();
        }
        contents_[size++] = entry;
    }
    TreeCursorEntry Pop() {
        return contents_[--size];
    }
    [[nodiscard]] TreeCursorEntry& Back() {
        return contents_[size - 1];
    }
    [[nodiscard]] const TreeCursorEntry& Back() const {
        return contents_[size - 1];
    }
    [[nodiscard]] const TreeCursorEntry& operator[](std::uint32_t index) const {
        return contents_[index];
    }

    std::uint32_t size = 0;

  private:
    void Grow() {
        const std::uint32_t capacity = capacity_ * 2;
        auto* const         grown    = static_cast<TreeCursorEntry*>(
            contents_ == inline_ ? std::malloc(capacity * sizeof(TreeCursorEntry))
                                 : std::realloc(contents_, capacity * sizeof(TreeCursorEntry)));
        if (contents_ == inline_) {
            std::memcpy(grown, inline_, size * sizeof(TreeCursorEntry));
        }
        contents_ = grown;
        capacity_ = capacity;
    }

    static constexpr std::uint32_t kInlineCapacity = 32;

    TreeCursorEntry  inline_[kInlineCapacity];
    TreeCursorEntry* contents_ = inline_;
    std::uint32_t    capacity_ = kInlineCapacity;
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
    CursorStack               stack_;
};

} // namespace ned::editor::parse
