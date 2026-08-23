//
// A trie over KeyChord sequences, mapping a full sequence (e.g. "C-x C-s") to
// a command name. Emacs-style prefix keys fall out naturally: binding
// "C-x C-s" creates an internal "C-x" node with no command of its own and a
// "C-s" child that has one.
//
// KeymapStack composes several Keymaps in priority order for global +
// major-mode + minor-mode layering (Phase 5 owns the actual Mode concept that
// picks which layers are active; this just knows how to combine layers it's
// given).
//

#ifndef NED_EDITOR_KEYMAP_H
#define NED_EDITOR_KEYMAP_H

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "Key.h"

namespace ned::editor {

class Keymap {
  public:
    Keymap() = default;

    void Bind(const std::vector<KeyChord>& sequence, std::string commandName);
    void Unbind(const std::vector<KeyChord>& sequence);

    enum class LookupResult {
        NoMatch, // sequence isn't bound and isn't a prefix of anything bound
        Prefix,  // sequence isn't bound itself, but a longer sequence starting with it is
        Match,   // sequence is bound to a command
    };

    struct Lookup {
        LookupResult result;
        std::string  commandName; // valid only when result == Match
    };

    [[nodiscard]] Lookup Resolve(const std::vector<KeyChord>& sequence) const;

    // Every bound sequence that is also a strict prefix of at least one
    // longer bound sequence, formatted via FormatKeySequence -- these are
    // structurally unreachable by typing (Resolve fires the shorter Match
    // before ever consulting the longer children; see Keymap.cpp's Resolve).
    // Diagnostic only: doesn't change Bind/Resolve behavior, just surfaces
    // the class of bug CommandsTest.cpp's keymap-collision regression test
    // checks the shipped default keymap against.
    [[nodiscard]] std::vector<std::string> AmbiguousBindings() const;

  private:
    // Every existing Mode factory constructs an empty Keymap() and nothing
    // pre-existing ever copy-constructs a Mode (always moved/RVO'd), so this
    // never mattered until the dynamic-grammar-loading follow-up's Mode
    // registry needed to hand back a real, independent copy of a stored
    // Mode. std::unique_ptr makes the implicit copy constructor/assignment
    // ill-formed by default; explicit deep-copy support here is what makes
    // Keymap (and Mode, which holds one by value) actually the "freely-
    // copyable value type" this codebase already documents it as elsewhere.
    struct Node {
        std::optional<std::string>                command;
        std::map<KeyChord, std::unique_ptr<Node>> children;

        Node()                           = default;
        Node(Node&&) noexcept            = default;
        Node& operator=(Node&&) noexcept = default;

        Node(const Node& other) : command(other.command) {
            for (const auto& [key, child] : other.children) {
                children.emplace(key, std::make_unique<Node>(*child));
            }
        }

        Node& operator=(const Node& other) {
            if (this != &other) {
                *this = Node(other);
            }
            return *this;
        }
    };

    static void CollectAmbiguousBindings(const Node& node, std::vector<KeyChord>& sequence, std::vector<std::string>& out);

    Node root_;
};

// Tries each layer in priority order (index 0 = highest priority) for a full
// resolution of the given sequence. This is a simpler policy than Emacs' true
// keymap-parent merging, but handles the common case correctly: a Match in
// any layer wins (first layer checked, in order); if no layer matches but at
// least one says the sequence is a valid prefix, the whole stack reports
// Prefix so the caller keeps collecting keys.
class KeymapStack {
  public:
    explicit KeymapStack(std::vector<const Keymap*> layers);

    [[nodiscard]] Keymap::Lookup Resolve(const std::vector<KeyChord>& sequence) const;

  private:
    std::vector<const Keymap*> layers_;
};

} // namespace ned::editor

#endif // NED_EDITOR_KEYMAP_H
