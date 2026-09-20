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

KeymapStack::KeymapStack(std::vector<const Keymap*> layers) : layers_(std::move(layers)) {
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

std::vector<Keymap::Binding> KeymapStack::AllBindings() const {
    std::vector<Keymap::Binding> reachable;

    for (const Keymap* layer : layers_) {
        for (Keymap::Binding& binding : layer->AllBindings()) {
            const Keymap::Lookup lookup = Resolve(binding.sequence);
            if (lookup.result != Keymap::LookupResult::Match || lookup.commandName != binding.commandName) {
                continue; // a higher-priority layer owns this sequence
            }
            // A strict prefix that resolves anywhere in the stack fires
            // first, so nothing can ever finish typing this sequence.
            bool shadowedByPrefix = false;
            for (std::size_t length = 1; length < binding.sequence.size(); ++length) {
                const std::vector<KeyChord> prefix(binding.sequence.begin(), binding.sequence.begin() + static_cast<std::ptrdiff_t>(length));
                if (Resolve(prefix).result == Keymap::LookupResult::Match) {
                    shadowedByPrefix = true;
                    break;
                }
            }
            if (!shadowedByPrefix) {
                reachable.push_back(std::move(binding));
            }
        }
    }

    return reachable;
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
