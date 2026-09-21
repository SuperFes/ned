#include "CompletionSources.h"

#include <algorithm>
#include <unordered_set>

#include "DabbrevComplete.h"
#include "FuzzyMatch.h"
#include "SnippetRegistry.h"

namespace ned::editor {

namespace {

    // LSP CompletionItemKind values, used as the shared kind vocabulary --
    // see Completion::kind. Text is what a buffer-scanned word honestly is,
    // Function what every ned/* binding is, Snippet what the registry holds.
    constexpr int kKindText     = 1;
    constexpr int kKindFunction = 3;
    constexpr int kKindSnippet  = 15;

    Completion LocalCompletion(std::string label, std::string insertText, CompletionSource source, int kind) {
        return Completion{
            .label      = label,
            .filterText = label,
            .sortText   = std::move(label),
            .insertText = std::move(insertText),
            .source     = source,
            .kind       = kind,
        };
    }

} // namespace

std::vector<Completion> BufferWordCompletions(std::string_view content, std::size_t point, std::string_view prefix,
                                              std::string_view extraWordCharacters, std::size_t maxCandidates) {
    if (prefix.empty()) {
        return {};
    }
    std::vector<std::string> words = CollectDabbrevCandidates(content, point, prefix, maxCandidates, extraWordCharacters);

    std::vector<Completion> completions;
    completions.reserve(words.size());
    for (std::string& word : words) {
        completions.push_back(LocalCompletion(word, word, CompletionSource::BufferWord, kKindText));
    }
    return completions;
}

std::vector<Completion> SnippetCompletions(const std::string& languageKey, std::string_view prefix) {
    if (prefix.empty()) {
        return {};
    }
    const std::vector<std::string> triggers = FuzzyFilterAndRank(SnippetTriggers(languageKey), prefix);

    std::vector<Completion> completions;
    completions.reserve(triggers.size());
    for (const std::string& trigger : triggers) {
        // A trigger SnippetTriggers just listed can still miss here if the
        // registry changed under us between the two calls (ned/register-snippet
        // from a running command); skipping is the only honest answer, since
        // a snippet candidate with no body would insert nothing.
        const std::optional<std::string> body = SnippetBodyForTrigger(languageKey, trigger);
        if (!body) {
            continue;
        }
        Completion completion = LocalCompletion(trigger, *body, CompletionSource::Snippet, kKindSnippet);
        completion.isSnippet  = true;
        completions.push_back(std::move(completion));
    }
    return completions;
}

std::vector<Completion> JanetBindingCompletions(const std::vector<std::string>& names, std::string_view prefix) {
    if (prefix.empty()) {
        return {};
    }
    const std::vector<std::string> ranked = FuzzyFilterAndRank(names, prefix);

    std::vector<Completion> completions;
    completions.reserve(ranked.size());
    for (const std::string& name : ranked) {
        // A subsequence match can never be shorter than the query it matched
        // against, so name.size() == prefix.size() here only when name IS
        // prefix verbatim -- point already sits right after a complete
        // binding name, nothing left to suggest.
        if (name.size() == prefix.size()) {
            continue;
        }
        completions.push_back(LocalCompletion(name, name, CompletionSource::JanetBinding, kKindFunction));
    }
    return completions;
}

std::vector<Completion> MergeCompletions(std::vector<std::vector<Completion>> lists) {
    std::stable_sort(lists.begin(), lists.end(), [](const std::vector<Completion>& lhs, const std::vector<Completion>& rhs) {
        // An empty list has no source to rank by and sorts last, where it
        // costs nothing.
        if (lhs.empty() || rhs.empty()) {
            return !lhs.empty();
        }
        return CompletionSourceRank(lhs.front().source) < CompletionSourceRank(rhs.front().source);
    });

    std::vector<Completion>         merged;
    std::unordered_set<std::string> claimed; // labels a higher-ranked source already took
    for (std::vector<Completion>& list : lists) {
        // Labels this list contributes are claimed only *after* it is fully
        // appended, so a source never suppresses its own duplicates.
        std::vector<std::string> contributed;
        contributed.reserve(list.size());
        for (Completion& completion : list) {
            if (claimed.contains(completion.label)) {
                continue;
            }
            contributed.push_back(completion.label);
            merged.push_back(std::move(completion));
        }
        claimed.insert(std::make_move_iterator(contributed.begin()), std::make_move_iterator(contributed.end()));
    }
    return merged;
}

} // namespace ned::editor
