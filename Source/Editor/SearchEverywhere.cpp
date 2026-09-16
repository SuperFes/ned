#include "Editor/SearchEverywhere.h"

#include <algorithm>

#include "Editor/FuzzyMatch.h"

namespace ned::editor {

std::vector<SearchEverywhereResult> RankSearchEverywhere(const std::vector<SearchEverywhereCandidate>& candidates,
                                                          std::string_view                              query,
                                                          std::optional<SearchEverywhereKind>           kindFilter) {
    std::vector<SearchEverywhereResult> results;
    results.reserve(candidates.size());
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        const SearchEverywhereCandidate& candidate = candidates[i];
        if (kindFilter && candidate.kind != *kindFilter) {
            continue;
        }
        if (const std::optional<int> score = FuzzyScore(candidate.label, query)) {
            results.push_back({.candidateIndex = i, .score = *score});
        }
    }

    std::sort(results.begin(), results.end(), [&candidates](const SearchEverywhereResult& a, const SearchEverywhereResult& b) {
        if (a.score != b.score) {
            return a.score > b.score;
        }
        const SearchEverywhereCandidate& ca = candidates[a.candidateIndex];
        const SearchEverywhereCandidate& cb = candidates[b.candidateIndex];
        if (ca.kind != cb.kind) {
            return ca.kind < cb.kind;
        }
        return ca.label < cb.label;
    });

    return results;
}

} // namespace ned::editor
