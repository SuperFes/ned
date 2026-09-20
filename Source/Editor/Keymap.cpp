#include "Keymap.h"

#include <algorithm>
#include <cstddef>
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

void Keymap::CollectBindings(const Node& node, std::vector<KeyChord>& sequence, std::vector<Binding>& out) {
    if (node.command) {
        out.push_back({sequence, *node.command});
    }
    for (const auto& [chord, child] : node.children) {
        sequence.push_back(chord);
        CollectBindings(*child, sequence, out);
        sequence.pop_back();
    }
}

std::vector<Keymap::Binding> Keymap::AllBindings() const {
    std::vector<Binding>  out;
    std::vector<KeyChord> sequence;
    CollectBindings(root_, sequence, out);
    return out;
}

KeymapStack::KeymapStack(std::vector<const Keymap*> layers, std::vector<std::string> layerNames) : layers_(std::move(layers)), layerNames_(std::move(layerNames)) {
}

std::size_t KeymapStack::LayerCount() const {
    return layers_.size();
}

std::string KeymapStack::LayerName(std::size_t layer) const {
    if (layer < layerNames_.size() && !layerNames_[layer].empty()) {
        return layerNames_[layer];
    }
    return "Layer " + std::to_string(layer);
}

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

namespace {

    // Empty when the binding is reachable, otherwise the command that fires
    // instead of it. Two ways to lose: a higher-priority layer owns the same
    // sequence, or a strict prefix of it Matches anywhere in the stack (the
    // Dispatcher fires that shorter command before the sequence can ever be
    // completed).
    std::string ShadowingCommand(const KeymapStack& stack, const Keymap::Binding& binding) {
        // Always a Match: the binding came out of one of these layers.
        const Keymap::Lookup lookup = stack.Resolve(binding.sequence);
        if (lookup.commandName != binding.commandName) {
            return lookup.commandName;
        }
        for (std::size_t length = 1; length < binding.sequence.size(); ++length) {
            const std::vector<KeyChord> prefix(binding.sequence.begin(), binding.sequence.begin() + static_cast<std::ptrdiff_t>(length));
            const Keymap::Lookup        prefixLookup = stack.Resolve(prefix);
            if (prefixLookup.result == Keymap::LookupResult::Match) {
                return prefixLookup.commandName;
            }
        }
        return {};
    }

} // namespace

std::vector<Keymap::Binding> KeymapStack::AllBindings() const {
    std::vector<Keymap::Binding> reachable;

    for (std::size_t index = 0; index < layers_.size(); ++index) {
        for (Keymap::Binding& binding : layers_[index]->AllBindings()) {
            binding.layer = index;
            if (ShadowingCommand(*this, binding).empty()) {
                reachable.push_back(std::move(binding));
            }
        }
    }

    return reachable;
}

std::vector<KeymapStack::ShadowedBinding> KeymapStack::ShadowedBindings() const {
    std::vector<ShadowedBinding> shadowed;

    for (std::size_t index = 0; index < layers_.size(); ++index) {
        for (Keymap::Binding& binding : layers_[index]->AllBindings()) {
            binding.layer  = index;
            std::string by = ShadowingCommand(*this, binding);
            if (!by.empty()) {
                shadowed.push_back(ShadowedBinding{.binding = std::move(binding), .shadowedBy = std::move(by)});
            }
        }
    }

    return shadowed;
}

std::map<std::string, std::string> ShortestBindingPerCommand(const KeymapStack& keymaps) {
    std::map<std::string, std::vector<KeyChord>> shortest;

    for (const Keymap::Binding& binding : keymaps.AllBindings()) {
        const auto [it, inserted] = shortest.emplace(binding.commandName, binding.sequence);
        if (inserted) {
            continue;
        }
        if (binding.sequence.size() < it->second.size() ||
            (binding.sequence.size() == it->second.size() && FormatKeySequence(binding.sequence) < FormatKeySequence(it->second))) {
            it->second = binding.sequence;
        }
    }

    std::map<std::string, std::string> formatted;
    for (const auto& [commandName, sequence] : shortest) {
        formatted.emplace(commandName, FormatKeySequence(sequence));
    }
    return formatted;
}

} // namespace ned::editor
