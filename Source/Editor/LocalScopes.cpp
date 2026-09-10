#include "LocalScopes.h"

#include <algorithm>
#include <cctype>

namespace ned::editor::locals {

namespace {

    bool Contains(Range outer, Range inner) {
        return outer.first <= inner.first && inner.second <= outer.second;
    }

    // A "scope" covering the entire buffer is indistinguishable from no
    // scope at all as far as this file is concerned, and treating it as a
    // real one would make a file-level binding look local and therefore
    // safe to rename in isolation -- which is exactly the mistake this
    // whole module exists to avoid. Every bundled locals.scm deliberately
    // leaves the file root uncaptured for the same reason; this is the
    // defence for a dlopen'd grammar's own upstream query, which may not.
    bool CoversWholeBuffer(Range scope, std::size_t textLength) {
        return scope.first == 0 && scope.second >= textLength;
    }

    // The captured text has to be a single plain identifier for any of this
    // to mean anything -- a query capturing a larger node (a whole
    // declarator, a pattern) would otherwise contribute ranges a rename
    // would happily rewrite wholesale.
    bool LooksLikeIdentifier(std::string_view text) {
        if (text.empty()) {
            return false;
        }
        return std::none_of(text.begin(), text.end(), [](unsigned char c) { return std::isspace(c) != 0; });
    }

    struct Occurrence {
        Range       range;
        bool        isDefinition;
        std::string qualifier;
    };

    // Index into a scope vector, or nullopt for file level. Used instead of
    // the Range itself so two scope captures that happen to share a range
    // stay distinguishable, and so "same owner" is one integer comparison.
    using ScopeRef = std::optional<std::size_t>;

} // namespace

std::optional<LocalBinding> ResolveBindingAt(std::span<const LocalCapture> captures, std::string_view bufferText,
                                             std::size_t point) {
    std::vector<Range>      scopes;
    std::vector<Occurrence> occurrences;
    for (const LocalCapture& capture : captures) {
        if (capture.startByte > capture.endByte || capture.endByte > bufferText.size()) {
            continue; // a stale capture list against shorter text -- never trusted, never fatal
        }
        const Range range{capture.startByte, capture.endByte};
        if (capture.kind == LocalCaptureKind::Scope) {
            if (!CoversWholeBuffer(range, bufferText.size())) {
                scopes.push_back(range);
            }
            continue;
        }
        occurrences.push_back(
            Occurrence{range, capture.kind == LocalCaptureKind::Definition, capture.qualifier});
    }
    std::sort(scopes.begin(), scopes.end());
    scopes.erase(std::unique(scopes.begin(), scopes.end()), scopes.end());

    // The token under point: the smallest definition/reference capture
    // covering it. Smallest so a query that captures both an inner
    // identifier and something wrapping it resolves to the identifier.
    const Occurrence* token = nullptr;
    for (const Occurrence& occurrence : occurrences) {
        if (occurrence.range.first > point || point > occurrence.range.second) {
            continue;
        }
        if (token == nullptr || (occurrence.range.second - occurrence.range.first) <
                                    (token->range.second - token->range.first)) {
            token = &occurrence;
        }
    }
    if (token == nullptr) {
        return std::nullopt;
    }
    const std::string name(bufferText.substr(token->range.first, token->range.second - token->range.first));
    if (!LooksLikeIdentifier(name)) {
        return std::nullopt;
    }

    // Every same-named occurrence, and each one's own innermost enclosing
    // scope -- computed once here rather than re-derived per lookup below.
    const auto innermostScopeOf = [&scopes](Range range) -> ScopeRef {
        ScopeRef best;
        for (std::size_t i = 0; i < scopes.size(); ++i) {
            if (!Contains(scopes[i], range)) {
                continue;
            }
            if (!best || (scopes[i].second - scopes[i].first) < (scopes[*best].second - scopes[*best].first)) {
                best = i;
            }
        }
        return best;
    };

    struct Candidate {
        const Occurrence* occurrence;
        ScopeRef          scope;
    };
    std::vector<Candidate> sameName;
    for (const Occurrence& occurrence : occurrences) {
        if (occurrence.range.second - occurrence.range.first != name.size()) {
            continue;
        }
        if (bufferText.compare(occurrence.range.first, name.size(), name) != 0) {
            continue;
        }
        sameName.push_back(Candidate{&occurrence, innermostScopeOf(occurrence.range)});
    }

    // Which binding a use resolves to: the innermost enclosing scope that
    // directly owns a same-named definition starting at or before the use,
    // falling through to file level. See this module's header for why the
    // position test is there and what it costs.
    //
    // The position test only ever disambiguates BETWEEN bindings, so when
    // the ordered walk finds no binding at all the whole walk is retried
    // ignoring position: with nothing outer to confuse it with, a use that
    // textually precedes the only definition of its name can only mean that
    // definition, in every language. That second pass is what makes a
    // Python comprehension work -- `[n * n for n in xs]` reads n twice
    // before binding it, and nothing else in the file binds n -- without
    // giving up the ordered walk where it actually matters.
    const auto ownerFor = [&](Range use, bool respectPosition) -> std::optional<ScopeRef> {
        std::vector<std::size_t> chain;
        for (std::size_t i = 0; i < scopes.size(); ++i) {
            if (Contains(scopes[i], use)) {
                chain.push_back(i);
            }
        }
        std::sort(chain.begin(), chain.end(), [&scopes](std::size_t a, std::size_t b) {
            return (scopes[a].second - scopes[a].first) < (scopes[b].second - scopes[b].first);
        });
        const auto boundIn = [&](ScopeRef scope) {
            return std::any_of(sameName.begin(), sameName.end(), [&](const Candidate& candidate) {
                return candidate.occurrence->isDefinition && candidate.scope == scope &&
                       (!respectPosition || candidate.occurrence->range.first <= use.first);
            });
        };
        for (const std::size_t index : chain) {
            if (boundIn(index)) {
                return ScopeRef(index);
            }
        }
        if (boundIn(ScopeRef{})) {
            return ScopeRef{};
        }
        return std::nullopt;
    };
    const auto resolve = [&](Range use) -> std::optional<ScopeRef> {
        if (const std::optional<ScopeRef> ordered = ownerFor(use, /*respectPosition=*/true)) {
            return ordered;
        }
        return ownerFor(use, /*respectPosition=*/false);
    };

    const std::optional<ScopeRef> owner = resolve(token->range);
    if (!owner) {
        return std::nullopt; // nothing in this file binds that name
    }

    LocalBinding binding;
    binding.name        = name;
    binding.scopeIsFile = !owner->has_value();
    if (owner->has_value()) {
        binding.scope = scopes[**owner];
    }

    bool seenDefinition = false;
    for (const Candidate& candidate : sameName) {
        if (resolve(candidate.occurrence->range) != owner) {
            continue;
        }
        binding.occurrences.push_back(candidate.occurrence->range);
        if (candidate.occurrence->isDefinition && candidate.scope == *owner &&
            (!seenDefinition || candidate.occurrence->range.first < binding.definition.first)) {
            binding.definition = candidate.occurrence->range;
            binding.qualifier  = candidate.occurrence->qualifier;
            seenDefinition     = true;
        }
    }
    if (!seenDefinition) {
        return std::nullopt; // ownerFor found one; not reachable, but never assumed
    }
    std::sort(binding.occurrences.begin(), binding.occurrences.end());
    binding.occurrences.erase(std::unique(binding.occurrences.begin(), binding.occurrences.end()),
                              binding.occurrences.end());

    // The position rule's own blind spot, reported rather than hidden: a
    // same-named use sitting inside this binding's scope but before its
    // definition, which resolved to an ENCLOSING binding. A use resolving
    // to a NESTED one is an ordinary independent inner scope, not this.
    const Range scopeRange = binding.scope.value_or(Range{0, bufferText.size()});
    for (const Candidate& candidate : sameName) {
        if (candidate.occurrence->range.first >= binding.definition.first) {
            continue;
        }
        if (!Contains(scopeRange, candidate.occurrence->range)) {
            continue;
        }
        const std::optional<ScopeRef> candidateOwner = resolve(candidate.occurrence->range);
        if (!candidateOwner || *candidateOwner == *owner) {
            continue;
        }
        const bool enclosesOurs =
            !candidateOwner->has_value() || (owner->has_value() && Contains(scopes[**candidateOwner], scopeRange));
        if (enclosesOurs) {
            binding.usedBeforeDefinition = true;
            break;
        }
    }
    return binding;
}

} // namespace ned::editor::locals
