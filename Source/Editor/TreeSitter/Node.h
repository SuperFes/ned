//
// A thin, value-type wrapper around tree-sitter's TSNode (tree-sitter
// foundation follow-up) -- the first piece of this project's own RAII C++
// layer over tree-sitter's C API, the same "wrap the C library behind an
// idiomatic C++ layer" approach Source/Janet/ already established for Janet
// (see Value.h/Environment.h). Deliberately hand-rolled rather than adopting
// one of the small existing community C++ wrappers (cpp-tree-sitter and
// similar) -- none are mature/maintained enough to build a core subsystem on,
// the same judgment call already made for TermOx over notcurses/FTXUI's own
// C++ bindings.
//
// TSNode itself is a small POD struct (not a pointer), safe to copy freely --
// but it holds a non-owning pointer back into the TSTree it came from, so a
// Node must not outlive the ned::editor::treesitter::Tree it was obtained
// from (see Tree.h). This mirrors the C API's own documented contract
// exactly; nothing here can enforce it at compile time.
//

#ifndef NED_EDITOR_TREESITTER_NODE_H
#define NED_EDITOR_TREESITTER_NODE_H

#include <cstddef>
#include <string_view>

#include <tree_sitter/api.h>

namespace ned::editor::treesitter {

class Node {
  public:
    explicit Node(TSNode node) noexcept;

    // The grammar's node type name (e.g. "string", "identifier") -- a pointer
    // into tree-sitter's own static string table, valid for the process
    // lifetime, not just this Node's.
    [[nodiscard]] std::string_view Type() const;

    [[nodiscard]] std::size_t StartByte() const;
    [[nodiscard]] std::size_t EndByte() const;

    [[nodiscard]] std::size_t ChildCount() const;
    [[nodiscard]] Node        Child(std::size_t index) const; // precondition: index < ChildCount()

    // structural-selection-expansion follow-up. True for a real grammar rule
    // (e.g. "binary_expression"), false for an anonymous/punctuation token
    // (e.g. a literal ";" or "("). NamedDescendantForByteRange/Parent below
    // are meant to be walked together, named-node-only, so a selection
    // expansion step never stops on a lone punctuation token.
    [[nodiscard]] bool IsNamed() const;

    // The immediate parent, or a null Node (see IsNull()) at the root.
    [[nodiscard]] Node Parent() const;

    // Emacs-keymap-round-2 follow-up (forward-sexp/backward-sexp): the next/
    // previous named sibling at this node's own level, or a null Node (see
    // IsNull()) if there isn't one -- unlike Parent()/
    // NamedDescendantForByteRange, which only walk *up*, these are what
    // sexp motion needs to move *sideways* between sibling forms.
    [[nodiscard]] Node NextNamedSibling() const;
    [[nodiscard]] Node PrevNamedSibling() const;

    // The smallest named node whose byte range fully contains [start, end].
    // A null Node (see IsNull()) if the tree has no such node (e.g. an
    // out-of-range request).
    [[nodiscard]] Node NamedDescendantForByteRange(std::size_t start, std::size_t end) const;

    // True for a node returned by a failed/out-of-range lookup (e.g.
    // Tree::RootNode() on a parse that produced no tree) -- every other
    // accessor is only meaningful when this is false.
    [[nodiscard]] bool IsNull() const;

    // The raw TSNode, for code in this directory (Query.cpp) that needs to
    // call further tree-sitter C functions this wrapper doesn't expose yet.
    // Not for use outside Source/Editor/TreeSitter/.
    [[nodiscard]] TSNode Raw() const noexcept;

  private:
    TSNode node_;
};

} // namespace ned::editor::treesitter

#endif // NED_EDITOR_TREESITTER_NODE_H
