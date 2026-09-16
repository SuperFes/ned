#include <catch2/catch_test_macros.hpp>

#include "Editor/SearchEverywhere.h"

using ned::editor::RankSearchEverywhere;
using ned::editor::SearchEverywhereCandidate;
using ned::editor::SearchEverywhereKind;
using ned::editor::SearchEverywhereResult;

namespace {

std::vector<std::string> Labels(const std::vector<SearchEverywhereResult>&    results,
                                 const std::vector<SearchEverywhereCandidate>& candidates) {
    std::vector<std::string> labels;
    labels.reserve(results.size());
    for (const SearchEverywhereResult& result : results) {
        labels.push_back(candidates[result.candidateIndex].label);
    }
    return labels;
}

} // namespace

TEST_CASE("RankSearchEverywhere with no candidates returns nothing", "[SearchEverywhere]") {
    REQUIRE(RankSearchEverywhere({}, "anything", std::nullopt).empty());
}

TEST_CASE("RankSearchEverywhere with an empty query returns every candidate, grouped by kind then alpha",
          "[SearchEverywhere]") {
    const std::vector<SearchEverywhereCandidate> candidates = {
        {SearchEverywhereKind::File, "zeta.cpp", "src/zeta.cpp"},
        {SearchEverywhereKind::Command, "save-buffer", "Save the current buffer."},
        {SearchEverywhereKind::Command, "delete-window", ""},
        {SearchEverywhereKind::Macro, "greet", ""},
    };
    const std::vector<SearchEverywhereResult> ranked = RankSearchEverywhere(candidates, "", std::nullopt);
    REQUIRE(Labels(ranked, candidates) ==
            std::vector<std::string>{"delete-window", "save-buffer", "greet", "zeta.cpp"});
}

TEST_CASE("RankSearchEverywhere excludes non-matching candidates entirely", "[SearchEverywhere]") {
    const std::vector<SearchEverywhereCandidate> candidates = {
        {SearchEverywhereKind::Command, "find-file", ""},
        {SearchEverywhereKind::Command, "quit", ""},
    };
    const std::vector<SearchEverywhereResult> ranked = RankSearchEverywhere(candidates, "fi", std::nullopt);
    REQUIRE(Labels(ranked, candidates) == std::vector<std::string>{"find-file"});
}

TEST_CASE("RankSearchEverywhere honors a kind filter", "[SearchEverywhere]") {
    const std::vector<SearchEverywhereCandidate> candidates = {
        {SearchEverywhereKind::Command, "greet-user", ""},
        {SearchEverywhereKind::Macro, "greet", ""},
        {SearchEverywhereKind::File, "greeting.txt", ""},
    };
    const std::vector<SearchEverywhereResult> ranked = RankSearchEverywhere(candidates, "greet", SearchEverywhereKind::Macro);
    REQUIRE(Labels(ranked, candidates) == std::vector<std::string>{"greet"});
}

TEST_CASE("RankSearchEverywhere breaks a cross-kind score tie by kind declaration order, then label", "[SearchEverywhere]") {
    // "ab" matches both "abd"/"abc" identically regardless of kind, so a
    // same-scored File and Command candidate must resolve by kind order
    // (Command < Macro < File < Buffer) before falling back to label.
    const std::vector<SearchEverywhereCandidate> candidates = {
        {SearchEverywhereKind::File, "abd", ""},
        {SearchEverywhereKind::Command, "abc", ""},
    };
    const std::vector<SearchEverywhereResult> ranked = RankSearchEverywhere(candidates, "ab", std::nullopt);
    REQUIRE(Labels(ranked, candidates) == std::vector<std::string>{"abc", "abd"});
}
