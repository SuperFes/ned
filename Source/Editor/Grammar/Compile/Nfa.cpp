#include "Nfa.h"

#include <algorithm>
#include <utility>

namespace ned::editor::grammar::compile {

// --- CharacterSet ------------------------------------------------------------

CharacterSet CharacterSet::FromChar(std::uint32_t c) {
    CharacterSet set;
    set.ranges_.push_back({c, c + 1});
    return set;
}

CharacterSet CharacterSet::FromRange(std::uint32_t first, std::uint32_t last) {
    if (first > last)
        std::swap(first, last);
    CharacterSet set;
    set.ranges_.push_back({first, last + 1});
    return set;
}

CharacterSet CharacterSet::Negate() const {
    CharacterSet  result;
    std::uint32_t previousEnd = 0;
    for (const CodepointRange& range : ranges_) {
        if (previousEnd < range.start)
            result.ranges_.push_back({previousEnd, range.start});
        previousEnd = range.end;
    }
    if (previousEnd < kCodepointEnd)
        result.ranges_.push_back({previousEnd, kCodepointEnd});
    return result;
}

CharacterSet CharacterSet::AddChar(std::uint32_t c) const {
    CharacterSet result = *this;
    result.AddIntRange(0, c, c + 1);
    return result;
}

CharacterSet CharacterSet::AddRange(std::uint32_t first, std::uint32_t last) const {
    CharacterSet result = *this;
    result.AddIntRange(0, first, last + 1);
    return result;
}

CharacterSet CharacterSet::Add(const CharacterSet& other) const {
    CharacterSet result = *this;
    std::size_t  index  = 0;
    for (const CodepointRange& range : other.ranges_)
        index = result.AddIntRange(index, range.start, range.end);
    return result;
}

void CharacterSet::Assign(const CharacterSet& other) {
    ranges_ = other.ranges_;
}

std::size_t CharacterSet::AddIntRange(std::size_t i, std::uint32_t start, std::uint32_t end) {
    while (i < ranges_.size()) {
        CodepointRange& range = ranges_[i];
        if (range.start > end) {
            ranges_.insert(ranges_.begin() + static_cast<std::ptrdiff_t>(i), {start, end});
            return i;
        }
        if (range.end >= start) {
            range.end   = std::max(range.end, end);
            range.start = std::min(range.start, start);
            while (i + 1 < ranges_.size() && ranges_[i + 1].start <= ranges_[i].end) {
                ranges_[i].end = std::max(ranges_[i].end, ranges_[i + 1].end);
                ranges_.erase(ranges_.begin() + static_cast<std::ptrdiff_t>(i) + 1);
            }
            return i;
        }
        i++;
    }
    ranges_.push_back({start, end});
    return i;
}

bool CharacterSet::DoesIntersect(const CharacterSet& other) const {
    std::size_t left = 0, right = 0;
    while (left < ranges_.size() && right < other.ranges_.size()) {
        const CodepointRange& l = ranges_[left];
        const CodepointRange& r = other.ranges_[right];
        if (l.end <= r.start)
            left++;
        else if (l.start >= r.end)
            right++;
        else
            return true;
    }
    return false;
}

CharacterSet CharacterSet::RemoveIntersection(CharacterSet& other) {
    CharacterSet intersection;
    std::size_t  leftI = 0, rightI = 0;
    while (leftI < ranges_.size() && rightI < other.ranges_.size()) {
        CodepointRange& left  = ranges_[leftI];
        CodepointRange& right = other.ranges_[rightI];
        if (left.start < right.start) {
            if (left.end <= right.start) {
                leftI++;
                continue;
            }
            if (left.end < right.end) {
                intersection.ranges_.push_back({right.start, left.end});
                std::swap(left.end, right.start);
                leftI++;
            }
            else if (left.end == right.end) {
                intersection.ranges_.push_back(right);
                left.end = right.start;
                other.ranges_.erase(other.ranges_.begin() + static_cast<std::ptrdiff_t>(rightI));
            }
            else {
                intersection.ranges_.push_back(right);
                const CodepointRange newRange{left.start, right.start};
                left.start = right.end;
                ranges_.insert(ranges_.begin() + static_cast<std::ptrdiff_t>(leftI), newRange);
                other.ranges_.erase(other.ranges_.begin() + static_cast<std::ptrdiff_t>(rightI));
                leftI++;
            }
        }
        else if (left.start == right.start) {
            if (left.end < right.end) {
                intersection.ranges_.push_back(left);
                right.start = left.end;
                ranges_.erase(ranges_.begin() + static_cast<std::ptrdiff_t>(leftI));
            }
            else if (left.end == right.end) {
                intersection.ranges_.push_back(left);
                ranges_.erase(ranges_.begin() + static_cast<std::ptrdiff_t>(leftI));
                other.ranges_.erase(other.ranges_.begin() + static_cast<std::ptrdiff_t>(rightI));
            }
            else {
                intersection.ranges_.push_back(right);
                left.start = right.end;
                other.ranges_.erase(other.ranges_.begin() + static_cast<std::ptrdiff_t>(rightI));
            }
        }
        else {
            if (left.start >= right.end) {
                rightI++;
                continue;
            }
            if (left.end < right.end) {
                intersection.ranges_.push_back(left);
                const CodepointRange newRange{right.start, left.start};
                right.start = left.end;
                other.ranges_.insert(other.ranges_.begin() + static_cast<std::ptrdiff_t>(rightI), newRange);
                ranges_.erase(ranges_.begin() + static_cast<std::ptrdiff_t>(leftI));
                rightI++;
            }
            else if (left.end == right.end) {
                intersection.ranges_.push_back(left);
                right.end = left.start;
                ranges_.erase(ranges_.begin() + static_cast<std::ptrdiff_t>(leftI));
            }
            else {
                intersection.ranges_.push_back({left.start, right.end});
                std::swap(left.start, right.end);
                rightI++;
            }
        }
    }
    return intersection;
}

CharacterSet CharacterSet::Difference(CharacterSet other) const {
    CharacterSet result = *this;
    result.RemoveIntersection(other);
    return result;
}

bool CharacterSet::Contains(std::uint32_t c) const {
    return ContainsRange(c, c + 1);
}

bool CharacterSet::ContainsRange(std::uint32_t start, std::uint32_t end) const {
    // The first range not ending before `start`.
    const auto it = std::lower_bound(ranges_.begin(), ranges_.end(), start,
                                     [](const CodepointRange& probe, std::uint32_t s) { return probe.end <= s; });
    return it != ranges_.end() && it->start <= start && it->end >= end;
}

std::vector<std::uint32_t> CharacterSet::Codepoints() const {
    std::vector<std::uint32_t> out;
    for (const CodepointRange& range : ranges_)
        for (std::uint32_t c = range.start; c < range.end; ++c)
            out.push_back(c);
    return out;
}

CharacterSet CharacterSet::SimplifyIgnoring(const CharacterSet& ruledOut) const {
    CharacterSet                  result;
    std::optional<CodepointRange> prev;
    const auto                    flush = [&]() {
        if (prev)
            result.ranges_.push_back(*prev);
    };
    for (const CodepointRange& range : ranges_) {
        if (ruledOut.ContainsRange(range.start, range.end))
            continue;
        if (prev && ruledOut.ContainsRange(prev->end, range.start)) {
            prev->end = range.end;
            continue;
        }
        flush();
        prev = range;
    }
    flush();
    return result;
}

std::strong_ordering CharacterSet::Compare(const CharacterSet& other) const {
    std::uint64_t mine = 0, theirs = 0;
    for (const CodepointRange& r : ranges_)
        mine += r.end - r.start;
    for (const CodepointRange& r : other.ranges_)
        theirs += r.end - r.start;
    if (mine != theirs)
        return mine <=> theirs;
    for (std::size_t i = 0; i < ranges_.size() && i < other.ranges_.size(); ++i) {
        const CodepointRange& l = ranges_[i];
        const CodepointRange& r = other.ranges_[i];
        if (const auto c = (l.end - l.start) <=> (r.end - r.start); c != 0)
            return c;
        if (const auto c = l.start <=> r.start; c != 0)
            return c;
    }
    return std::strong_ordering::equal;
}

std::string CharacterSet::Describe() const {
    std::string out = "[";
    for (std::size_t i = 0; i < ranges_.size(); ++i) {
        if (i > 0)
            out += ", ";
        out += std::to_string(ranges_[i].start) + "-" + std::to_string(ranges_[i].end - 1);
    }
    return out + "]";
}

// --- NfaCursor ---------------------------------------------------------------

NfaCursor::NfaCursor(const Nfa& nfa, std::vector<std::uint32_t> states) : nfa_(&nfa) {
    AddStates(states);
}

void NfaCursor::Reset(std::vector<std::uint32_t> states) {
    stateIds_.clear();
    AddStates(states);
}

void NfaCursor::ForceReset(std::vector<std::uint32_t> states) {
    stateIds_ = std::move(states);
}

std::vector<std::pair<const CharacterSet*, bool>> NfaCursor::TransitionChars() const {
    std::vector<std::pair<const CharacterSet*, bool>> out;
    for (const std::uint32_t id : stateIds_) {
        const NfaState& state = nfa_->states[id];
        if (state.kind == NfaState::Kind::Advance)
            out.emplace_back(&state.chars, state.isSep);
    }
    return out;
}

std::vector<NfaTransition> NfaCursor::Transitions() const {
    std::vector<NfaTransition> result;
    for (const std::uint32_t id : stateIds_) {
        const NfaState& state = nfa_->states[id];
        if (state.kind != NfaState::Kind::Advance)
            continue;
        CharacterSet chars = state.chars;
        std::size_t  i     = 0;
        while (i < result.size() && !chars.IsEmpty()) {
            CharacterSet intersection = result[i].characters.RemoveIntersection(chars);
            if (!intersection.IsEmpty()) {
                std::vector<std::uint32_t> intersectionStates = result[i].states;
                const auto                 pos                = std::lower_bound(intersectionStates.begin(), intersectionStates.end(), state.stateId);
                if (pos == intersectionStates.end() || *pos != state.stateId)
                    intersectionStates.insert(pos, state.stateId);
                NfaTransition intersectionTransition{
                    .characters  = std::move(intersection),
                    .isSeparator = result[i].isSeparator && state.isSep,
                    .precedence  = std::max(result[i].precedence, state.precedence),
                    .states      = std::move(intersectionStates),
                };
                if (result[i].characters.IsEmpty()) {
                    result[i] = std::move(intersectionTransition);
                }
                else {
                    result.insert(result.begin() + static_cast<std::ptrdiff_t>(i), std::move(intersectionTransition));
                    i++;
                }
            }
            i++;
        }
        if (!chars.IsEmpty()) {
            result.push_back(NfaTransition{.characters = std::move(chars), .isSeparator = state.isSep, .precedence = state.precedence, .states = {state.stateId}});
        }
    }

    for (std::size_t i = 0; i < result.size(); ++i) {
        for (std::size_t j = 0; j < i; ++j) {
            if (result[j].states == result[i].states && result[j].isSeparator == result[i].isSeparator &&
                result[j].precedence == result[i].precedence) {
                result[j].characters = result[j].characters.Add(result[i].characters);
                result.erase(result.begin() + static_cast<std::ptrdiff_t>(i));
                i--;
                break;
            }
        }
    }

    std::sort(result.begin(), result.end(), [](const NfaTransition& a, const NfaTransition& b) { return a.characters < b.characters; });
    return result;
}

std::vector<std::pair<std::size_t, int>> NfaCursor::Completions() const {
    std::vector<std::pair<std::size_t, int>> out;
    for (const std::uint32_t id : stateIds_) {
        const NfaState& state = nfa_->states[id];
        if (state.kind == NfaState::Kind::Accept)
            out.emplace_back(state.variableIndex, state.precedence);
    }
    return out;
}

void NfaCursor::AddStates(std::vector<std::uint32_t>& newStateIds) {
    std::size_t i = 0;
    while (i < newStateIds.size()) {
        const std::uint32_t stateId = newStateIds[i];
        const NfaState&     state   = nfa_->states[stateId];
        if (state.kind == NfaState::Kind::Split) {
            bool hasLeft = false, hasRight = false;
            for (const std::uint32_t id : newStateIds) {
                if (id == state.left)
                    hasLeft = true;
                if (id == state.right)
                    hasRight = true;
            }
            if (!hasLeft)
                newStateIds.push_back(state.left);
            if (!hasRight)
                newStateIds.push_back(state.right);
        }
        else {
            const auto pos = std::lower_bound(stateIds_.begin(), stateIds_.end(), stateId);
            if (pos == stateIds_.end() || *pos != stateId)
                stateIds_.insert(pos, stateId);
        }
        i++;
    }
}

} // namespace ned::editor::grammar::compile
