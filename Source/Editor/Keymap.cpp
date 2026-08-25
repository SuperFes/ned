#include "Keymap.h"

#include <algorithm>
#include <utility>

namespace ned::editor {

void Keymap::Bind(const std::vector<KeyChord>& sequence, std::string commandName) {
    Node* node = &root_;

    for (const auto& chord : sequence) {
        auto& child = node->children[chord];
        if (!child) {
            child = std::make_unique<Node>();
        }
        node = child.get();
    }

    node->command = std::move(commandName);
}

void Keymap::Unbind(const std::vector<KeyChord>& sequence) {
    Node* node = &root_;

    for (const auto& chord : sequence) {
        const auto it = node->children.find(chord);
        if (it == node->children.end()) {
            return; // nothing bound at this sequence
        }
        node = it->second.get();
    }

    node->command.reset();
}

Keymap::Lookup Keymap::Resolve(const std::vector<KeyChord>& sequence) const {
    const Node* node = &root_;

    for (const auto& chord : sequence) {
        const auto it = node->children.find(chord);
        if (it == node->children.end()) {
            return {LookupResult::NoMatch, {}};
        }
        node = it->second.get();
    }

    if (node->command) {
        return {LookupResult::Match, *node->command};
    }
    if (!node->children.empty()) {
        return {LookupResult::Prefix, {}};
    }
    return {LookupResult::NoMatch, {}};
}

void Keymap::CollectAmbiguousBindings(const Node& node, std::vector<KeyChord>& sequence, std::vector<std::string>& out) {
    if (node.command && !node.children.empty()) {
        out.push_back(FormatKeySequence(sequence));
    }
    for (const auto& [chord, child] : node.children) {
        sequence.push_back(chord);
        CollectAmbiguousBindings(*child, sequence, out);
        sequence.pop_back();
    }
}

std::vector<std::string> Keymap::AmbiguousBindings() const {
    std::vector<std::string> out;
    std::vector<KeyChord>    sequence;
    CollectAmbiguousBindings(root_, sequence, out);
    return out;
}

std::vector<Keymap::ChildBinding> Keymap::ChildrenAt(const std::vector<KeyChord>& prefix) const {
    const Node* node = &root_;

    for (const auto& chord : prefix) {
        const auto it = node->children.find(chord);
        if (it == node->children.end()) {
            return {};
        }
        node = it->second.get();
    }

    std::vector<ChildBinding> out;
    out.reserve(node->children.size());
    for (const auto& [chord, child] : node->children) {
        out.push_back({chord, child->command});
    }
    return out;
}

KeymapStack::KeymapStack(std::vector<const Keymap*> layers) : layers_(std::move(layers)) {}

Keymap::Lookup KeymapStack::Resolve(const std::vector<KeyChord>& sequence) const {
    bool anyPrefix = false;

    for (const Keymap* layer : layers_) {
        const auto result = layer->Resolve(sequence);
        if (result.result == Keymap::LookupResult::Match) {
            return result;
        }
        if (result.result == Keymap::LookupResult::Prefix) {
            anyPrefix = true;
        }
    }

    return anyPrefix ? Keymap::Lookup{Keymap::LookupResult::Prefix, {}} : Keymap::Lookup{Keymap::LookupResult::NoMatch, {}};
}

std::vector<Keymap::ChildBinding> KeymapStack::ChildrenAt(const std::vector<KeyChord>& prefix) const {
    std::vector<Keymap::ChildBinding> merged;

    for (const Keymap* layer : layers_) {
        for (const auto& binding : layer->ChildrenAt(prefix)) {
            const bool alreadyPresent =
                std::any_of(merged.begin(), merged.end(), [&](const Keymap::ChildBinding& existing) { return existing.chord == binding.chord; });
            if (!alreadyPresent) {
                merged.push_back(binding);
            }
        }
    }

    return merged;
}

} // namespace ned::editor
