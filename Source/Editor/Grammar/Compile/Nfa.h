//
// Character sets and the lexical NFA -- the port of tree-sitter's
// generator `nfa.rs`. A CharacterSet is a sorted list of disjoint
// half-open codepoint ranges; the NFA is a flat state list where each token
// variable's states end at its start state (states are built backwards).
//

#ifndef NED_EDITOR_GRAMMAR_COMPILE_NFA_H
#define NED_EDITOR_GRAMMAR_COMPILE_NFA_H

#include <compare>
#include <cstdint>
#include <string>
#include <vector>

namespace ned::editor::grammar::compile {

inline constexpr std::uint32_t kCodepointEnd = 0x10FFFF + 1;

struct CodepointRange {
    std::uint32_t start = 0; // inclusive
    std::uint32_t end   = 0; // exclusive

    auto operator<=>(const CodepointRange&) const = default;
};

class CharacterSet {
  public:
    static CharacterSet Empty() {
        return {};
    }
    static CharacterSet FromChar(std::uint32_t c);
    static CharacterSet FromRange(std::uint32_t first, std::uint32_t last); // inclusive

    [[nodiscard]] CharacterSet Negate() const;
    [[nodiscard]] CharacterSet AddChar(std::uint32_t c) const;
    [[nodiscard]] CharacterSet AddRange(std::uint32_t first, std::uint32_t last) const; // inclusive
    [[nodiscard]] CharacterSet Add(const CharacterSet& other) const;
    void                       Assign(const CharacterSet& other);
    [[nodiscard]] bool         DoesIntersect(const CharacterSet& other) const;
    // The characters in both sets, removed from both operands.
    CharacterSet               RemoveIntersection(CharacterSet& other);
    [[nodiscard]] CharacterSet Difference(CharacterSet other) const;
    [[nodiscard]] std::size_t  RangeCount() const {
        return ranges_.size();
    }
    [[nodiscard]] const std::vector<CodepointRange>& Ranges() const {
        return ranges_;
    }
    [[nodiscard]] bool IsEmpty() const {
        return ranges_.empty();
    }
    [[nodiscard]] bool Contains(std::uint32_t c) const;
    [[nodiscard]] bool ContainsRange(std::uint32_t start, std::uint32_t end) const; // half-open
    // Every character in the set, in order (large sets are large).
    [[nodiscard]] std::vector<std::uint32_t> Codepoints() const;
    // Reduced ranges given that `ruledOut` characters need no checking.
    [[nodiscard]] CharacterSet SimplifyIgnoring(const CharacterSet& ruledOut) const;

    bool operator==(const CharacterSet&) const = default;
    // tree-sitter's ordering: by total size, then range by range.
    [[nodiscard]] std::strong_ordering Compare(const CharacterSet& other) const;
    bool                               operator<(const CharacterSet& other) const {
        return Compare(other) == std::strong_ordering::less;
    }
    [[nodiscard]] std::string Describe() const;

  private:
    std::size_t AddIntRange(std::size_t i, std::uint32_t start, std::uint32_t end);

    std::vector<CodepointRange> ranges_;
};

struct NfaState {
    enum class Kind : std::uint8_t { Advance,
                                     Split,
                                     Accept };
    Kind          kind = Kind::Accept;
    CharacterSet  chars;             // Advance
    std::uint32_t stateId       = 0; // Advance
    bool          isSep         = false;
    int           precedence    = 0; // Advance, Accept
    std::uint32_t left          = 0; // Split
    std::uint32_t right         = 0; // Split
    std::size_t   variableIndex = 0; // Accept

    static NfaState Advance(CharacterSet chars, std::uint32_t stateId, bool isSep, int precedence) {
        return {.kind = Kind::Advance, .chars = std::move(chars), .stateId = stateId, .isSep = isSep, .precedence = precedence};
    }
    static NfaState Split(std::uint32_t left, std::uint32_t right) {
        return {.kind = Kind::Split, .left = left, .right = right};
    }
    static NfaState Accept(std::size_t variableIndex, int precedence) {
        return {.kind = Kind::Accept, .precedence = precedence, .variableIndex = variableIndex};
    }
};

struct Nfa {
    std::vector<NfaState> states;

    [[nodiscard]] std::uint32_t LastStateId() const {
        return static_cast<std::uint32_t>(states.size()) - 1;
    }
};

struct NfaTransition {
    CharacterSet               characters;
    bool                       isSeparator = false;
    int                        precedence  = 0;
    std::vector<std::uint32_t> states; // sorted

    bool operator==(const NfaTransition&) const = default;
};

// A set of NFA states with split states already followed.
class NfaCursor {
  public:
    NfaCursor(const Nfa& nfa, std::vector<std::uint32_t> states);

    void Reset(std::vector<std::uint32_t> states);
    void ForceReset(std::vector<std::uint32_t> states);

    [[nodiscard]] const std::vector<std::uint32_t>& StateIds() const {
        return stateIds_;
    }
    // (chars, isSeparator) of every raw Advance state, in state order.
    [[nodiscard]] std::vector<std::pair<const CharacterSet*, bool>> TransitionChars() const;
    // Raw transitions grouped into disjoint character sets.
    [[nodiscard]] std::vector<NfaTransition> Transitions() const;
    // (variableIndex, precedence) of every Accept state, in state order.
    [[nodiscard]] std::vector<std::pair<std::size_t, int>> Completions() const;

  private:
    void AddStates(std::vector<std::uint32_t>& newStateIds);

    std::vector<std::uint32_t> stateIds_;
    const Nfa*                 nfa_;
};

} // namespace ned::editor::grammar::compile

#endif // NED_EDITOR_GRAMMAR_COMPILE_NFA_H
