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

#include <cstddef>
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

    // which-key follow-up: the direct children of the node reached by
    // `prefix` -- one entry per next possible chord, with commandName set
    // when that chord is itself bound (nullopt means it's a deeper prefix,
    // not yet a command). Empty if `prefix` isn't a valid prefix in this
    // layer at all (NoMatch or Match, not Prefix).
    struct ChildBinding {
        KeyChord                   chord;
        std::optional<std::string> commandName;
    };
    [[nodiscard]] std::vector<ChildBinding> ChildrenAt(const std::vector<KeyChord>& prefix) const;

    // The reverse of Bind: every bound sequence in this layer, paired with
    // the command it runs. Traversal order is the trie's own (std::map over
    // KeyChord), so it is deterministic but is not a useful sort for
    // display -- a caller that wants "the one chord to show for a command"
    // wants ShortestBindingPerCommand below, not this.
    struct Binding {
        std::vector<KeyChord> sequence;
        std::string           commandName;
        // Which stack layer this came from, filled in by KeymapStack::
        // AllBindings/ShadowedBindings -- always 0 from the single-layer
        // Keymap::AllBindings below, where there is nothing else it could be.
        std::size_t layer = 0;
    };
    [[nodiscard]] std::vector<Binding> AllBindings() const;

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
    static void CollectBindings(const Node& node, std::vector<KeyChord>& sequence, std::vector<Binding>& out);

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
    // layerNames is display-only (a help buffer's section headings) and is
    // allowed to be shorter than layers -- LayerName falls back to a generic
    // label, which is what every test constructing a bare stack gets.
    explicit KeymapStack(std::vector<const Keymap*> layers, std::vector<std::string> layerNames = {});

    [[nodiscard]] std::size_t LayerCount() const;
    [[nodiscard]] std::string LayerName(std::size_t layer) const;

    [[nodiscard]] Keymap::Lookup Resolve(const std::vector<KeyChord>& sequence) const;

    // Merges ChildrenAt across every layer -- first layer to bind a given
    // chord wins, mirroring Resolve's own "first Match wins" layer priority.
    [[nodiscard]] std::vector<Keymap::ChildBinding> ChildrenAt(const std::vector<KeyChord>& prefix) const;

    // Every binding across all layers that is actually *reachable* by
    // typing it. Two ways a bound sequence is not: a higher-priority layer
    // binds the same sequence to something else (Resolve's own first-Match-
    // wins), or any strict prefix of it Matches anywhere in the stack, in
    // which case Dispatcher fires that shorter command before the longer
    // sequence can ever be completed. The second is AmbiguousBindings'
    // within-a-layer diagnostic generalized across layers -- here it is a
    // filter rather than a report, because a caller displaying "the chord
    // for this command" must not show one that does nothing.
    [[nodiscard]] std::vector<Keymap::Binding> AllBindings() const;

    // The complement of AllBindings: every bound sequence that typing can
    // never reach, paired with the command that wins instead. AllBindings
    // drops these silently because a palette must not show a chord that
    // does nothing; describe-bindings reports them, since an unreachable
    // binding in a user's own init.janet is a bug they can only see if
    // something says so.
    struct ShadowedBinding {
        Keymap::Binding binding;    // the unreachable one
        std::string     shadowedBy; // the command that fires instead
    };
    [[nodiscard]] std::vector<ShadowedBinding> ShadowedBindings() const;

  private:
    std::vector<const Keymap*> layers_;
    std::vector<std::string>   layerNames_;
};

// One formatted chord sequence per command name, over KeymapStack::
// AllBindings -- what a palette row or a help buffer shows beside a command
// it lists. A command bound more than once keeps its shortest sequence
// (fewest chords, then FormatKeySequence order), on the grounds that the
// shortest is the one worth teaching; commands with no reachable binding
// are simply absent.
[[nodiscard]] std::map<std::string, std::string> ShortestBindingPerCommand(const KeymapStack& keymaps);

} // namespace ned::editor

#endif // NED_EDITOR_KEYMAP_H
