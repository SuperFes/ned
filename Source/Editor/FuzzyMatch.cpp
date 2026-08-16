#include "FuzzyMatch.h"

#include <algorithm>
#include <cctype>

namespace ned::editor {

namespace {

    constexpr int kMatchBase        = 1;  // per matched query character
    constexpr int kWordStartBonus   = 10; // matched char is at index 0 or after '-'/'_'
    constexpr int kConsecutiveBonus = 5;  // per additional char in an unbroken matched run
    constexpr int kGapPenalty       = 1;  // per candidate char skipped since the previous match

    char AsciiLower(char c) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    bool IsWordBoundaryBefore(std::string_view candidate, std::size_t pos) {
        return pos == 0 || candidate[pos - 1] == '-' || candidate[pos - 1] == '_';
    }

} // namespace

std::optional<int> FuzzyScore(std::string_view candidate, std::string_view query) {
    int         score          = 0;
    int         consecutiveRun = 0;
    std::size_t searchFrom     = 0;
    std::size_t lastMatchPos   = std::string_view::npos;

    for (const char rawQueryChar : query) {
        const char target = AsciiLower(rawQueryChar);

        std::size_t pos = std::string_view::npos;
        for (std::size_t i = searchFrom; i < candidate.size(); ++i) {
            if (AsciiLower(candidate[i]) == target) {
                pos = i;
                break;
            }
        }
        if (pos == std::string_view::npos) {
            return std::nullopt;
        }

        score -= static_cast<int>(pos - searchFrom) * kGapPenalty;
        if (IsWordBoundaryBefore(candidate, pos)) {
            score += kWordStartBonus;
        }
        if (lastMatchPos != std::string_view::npos && pos == lastMatchPos + 1) {
            ++consecutiveRun;
            score += kConsecutiveBonus * consecutiveRun;
        }
        else {
            consecutiveRun = 0;
        }
        score += kMatchBase;

        lastMatchPos = pos;
        searchFrom   = pos + 1;
    }

    return score;
}

std::vector<std::string> FuzzyFilterAndRank(const std::vector<std::string>& candidates, std::string_view query) {
    std::vector<std::pair<int, std::string>> scored;
    scored.reserve(candidates.size());
    for (const std::string& candidate : candidates) {
        if (const std::optional<int> score = FuzzyScore(candidate, query)) {
            scored.emplace_back(*score, candidate);
        }
    }

    std::sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) {
        if (a.first != b.first) {
            return a.first > b.first;
        }
        return a.second < b.second;
    });

    std::vector<std::string> result;
    result.reserve(scored.size());
    for (auto& [score, name] : scored) {
        result.push_back(std::move(name));
    }
    return result;
}

} // namespace ned::editor
