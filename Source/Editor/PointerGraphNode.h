//
// Debugging wishlist follow-up (pointer/linked-list graph view). One node in
// a DAP variable's field graph, as browsed by BufferView's pointer-graph
// session (Editor/ExpandableTree.h<PointerGraphNode>, mirroring the call/
// type-hierarchy browser's own ExpandableTree<ResolvedHierarchyItem> shape).
// Deliberately not DapManager::Variable itself -- variablesReference is
// overwritten to 0 on a detected cycle (see cyclic below), which would be
// the wrong thing to do to a real DapManager::Variable if one were ever
// reused elsewhere, and BufferView's own mapping step is what's responsible
// for that overwrite.
//
// Kept UI/DAP-free and pure so its one real bit of logic (the row label,
// including the cycle annotation) is unit-testable without a live adapter
// or terminal.
//

#ifndef NED_EDITOR_POINTERGRAPHNODE_H
#define NED_EDITOR_POINTERGRAPHNODE_H

#include <string>

namespace ned::editor {

struct PointerGraphNode {
    std::string name;
    std::string type; // empty if the adapter sent none, same convention as DapManager::Variable::type
    std::string value;
    std::string memoryReference;        // empty when the adapter didn't send one -- see DapManager::Variable's own doc comment
    int         variablesReference = 0; // > 0 means expandable; forced to 0 once cyclic is set (see below)

    // Set by BufferView::ExpandPointerGraphNode when this node's own
    // memoryReference matched an ancestor's already-visited memoryReference
    // in the same session -- a real linked/circular list can point back into
    // itself, unlike an LSP call/type hierarchy (acyclic by construction),
    // so the pointer-graph session needs this extra bit the hierarchy
    // browser never did. A cyclic node's variablesReference is always forced
    // to 0 by the same caller, which is what actually stops the tree from
    // growing forever -- this flag exists only so the row label can say why.
    bool cyclic = false;
};

// "name: type = value", with " (cycle)" appended when cyclic -- the pointer-
// graph TreeView's row label. Mirrors FormatDebugVariableLine's own
// name/type/value formatting (Source/UI/BufferView.cpp) minus the *debug*
// buffer's bracket markers, which have no meaning inside a TreeView row.
[[nodiscard]] std::string FormatPointerGraphLabel(const PointerGraphNode& node);

} // namespace ned::editor

#endif // NED_EDITOR_POINTERGRAPHNODE_H
