#include "Keymap.h"

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

} // namespace ned::editor
