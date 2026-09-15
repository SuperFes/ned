#include "WhitespaceHygiene.h"

#include <string_view>
#include <vector>

namespace ned::text {

std::string TrimTrailingWhitespaceAndBlankLines(std::string content) {
    if (content.empty()) {
        return content;
    }
    std::string trimmed;
    trimmed.reserve(content.size());
    std::size_t lineStart = 0;
    for (std::size_t i = 0; i <= content.size(); ++i) {
        if (i == content.size() || content[i] == '\n') {
            std::size_t lineEnd = i;
            while (lineEnd > lineStart && (content[lineEnd - 1] == ' ' || content[lineEnd - 1] == '\t')) {
                --lineEnd;
            }
            trimmed.append(content, lineStart, lineEnd - lineStart);
            if (i < content.size()) {
                trimmed.push_back('\n');
            }
            lineStart = i + 1;
        }
    }
    while (!trimmed.empty() && trimmed.back() == '\n') {
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

std::string CollapseBlankLineRuns(std::string content, int maxConsecutive) {
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
    for (const std::string_view& line : lines) {
        if (isBlank(line)) {
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
