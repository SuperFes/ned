#include "DabbrevComplete.h"

#include <algorithm>
#include <cctype>
#include <unordered_set>

namespace ned::editor {

namespace {

bool IsWordChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

struct Match {
    std::size_t start;
    std::string text;
};

} // namespace

std::vector<std::string> CollectDabbrevCandidates(std::string_view content, std::size_t point, std::string_view prefix,
                                                    std::size_t maxCandidates) {
    if (prefix.empty() || maxCandidates == 0) {
        return {};
    }

    std::vector<Match> before; // start < point, scan order is already nearest-to-point-last
    std::vector<Match> after;  // start > point, scan order is already nearest-first

    const std::size_t length = content.size();
    std::size_t       i      = 0;
    while (i < length) {
        if (!IsWordChar(content[i])) {
            ++i;
            continue;
        }
        const std::size_t start = i;
        while (i < length && IsWordChar(content[i])) {
            ++i;
        }
        const std::size_t wordLength = i - start;
        // wordLength > prefix.size() excludes the prefix's own not-yet-
        // completed occurrence at point, along with any other exact-prefix
        // match elsewhere -- neither would leave a suffix worth suggesting.
        if (wordLength > prefix.size() && content.compare(start, prefix.size(), prefix) == 0) {
            if (start < point) {
                before.push_back({start, std::string(content.substr(start, wordLength))});
            } else if (start > point) {
                after.push_back({start, std::string(content.substr(start, wordLength))});
            }
        }
    }

    std::sort(before.begin(), before.end(), [](const Match& a, const Match& b) { return a.start > b.start; });

    std::vector<std::string>        result;
    std::unordered_set<std::string> seen;
    auto                            append = [&](const std::vector<Match>& matches) {
        for (const Match& m : matches) {
            if (result.size() >= maxCandidates) {
                return;
            }
            if (seen.insert(m.text).second) {
                result.push_back(m.text);
            }
        }
    };
    append(before);
    append(after);
    return result;
}

} // namespace ned::editor
