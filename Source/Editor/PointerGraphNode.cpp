#include "Editor/PointerGraphNode.h"

namespace ned::editor {

std::string FormatPointerGraphLabel(const PointerGraphNode& node) {
    std::string label = node.name;
    if (!node.type.empty()) {
        label += ": " + node.type;
    }
    label += " = " + node.value;
    if (node.cyclic) {
        label += " (cycle)";
    }
    return label;
}

} // namespace ned::editor
