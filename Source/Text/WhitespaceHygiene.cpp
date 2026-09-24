#include "WhitespaceHygiene.h"

#include <algorithm>
#include <string_view>
#include <vector>

namespace ned::text {

std::string TrimTrailingWhitespaceAndBlankLines(std::string content, std::span<const std::size_t> keep) {
    if (content.empty()) {
        return content;
    }
    std::string trimmed;
    trimmed.reserve(content.size());
    std::size_t lineStart = 0;
    std::size_t line      = 0;
    std::size_t keptEnd   = 0; // trimmed's size just past the last kept line's content
    for (std::size_t i = 0; i <= content.size(); ++i) {
        if (i == content.size() || content[i] == '\n') {
            std::size_t lineEnd = i;
            const bool  kept    = std::binary_search(keep.begin(), keep.end(), line);
            while (!kept && lineEnd > lineStart && (content[lineEnd - 1] == ' ' || content[lineEnd - 1] == '\t')) {
                --lineEnd;
            }
            trimmed.append(content, lineStart, lineEnd - lineStart);
            if (i < content.size()) {
                trimmed.push_back('\n');
            }
            if (kept) {
                keptEnd = trimmed.size(); // its newline too: an open fence's last blank line is code
            }
            lineStart = i + 1;
            ++line;
        }
    }
    while (trimmed.size() > keptEnd && trimmed.back() == '\n') {
        trimmed.pop_back();
    }
    return trimmed;
}

std::string EnsureTrailingNewline(std::string content) {
    if (!content.empty() && content.back() != '\n') {
        content.push_back('\n');
    }
    return content;
}

std::string CollapseBlankLineRuns(std::string content, int maxConsecutive, std::span<const std::size_t> keep) {
    if (maxConsecutive < 0) {
        return content;
    }

    const auto isBlank = [](std::string_view line) {
        return line.find_first_not_of(" \t") == std::string_view::npos;
    };

    std::vector<std::string_view> lines;
    std::size_t                   lineStart = 0;
    for (std::size_t i = 0; i <= content.size(); ++i) {
        if (i == content.size() || content[i] == '\n') {
            lines.push_back(std::string_view(content).substr(lineStart, i - lineStart));
            lineStart = i + 1;
        }
    }

    std::string result;
    result.reserve(content.size());
    std::size_t consecutiveBlank = 0;
    bool        firstLine        = true;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        const std::string_view line = lines[index];
        if (std::binary_search(keep.begin(), keep.end(), index)) {
            consecutiveBlank = 0;
        }
        else if (isBlank(line)) {
            ++consecutiveBlank;
            if (consecutiveBlank > static_cast<std::size_t>(maxConsecutive)) {
                continue; // drop this line entirely -- the run is already at its cap
            }
        }
        else {
            consecutiveBlank = 0;
        }
        if (!firstLine) {
            result.push_back('\n');
        }
        result.append(line);
        firstLine = false;
    }
    return result;
}

} // namespace ned::text
