#include "IndentDetect.h"

#include <algorithm>
#include <climits>
#include <cstddef>

namespace ned::editor {

DetectedIndent DetectIndentStyle(std::string_view text) {
    std::size_t tabLines    = 0;
    std::size_t spaceLines  = 0;
    int         minSpaceRun = INT_MAX;

    std::size_t lineStart = 0;
    while (lineStart <= text.size()) {
        const std::size_t lineEnd = text.find('\n', lineStart);
        const std::string_view line = text.substr(lineStart, (lineEnd == std::string_view::npos ? text.size() : lineEnd) - lineStart);

        if (!line.empty() && (line.front() == '\t' || line.front() == ' ')) {
            const char  leader = line.front();
            std::size_t run    = 0;
            while (run < line.size() && line[run] == leader) {
                ++run;
            }
            // A line of pure whitespace carries no indentation information
            // (it indents nothing) -- only a line with real content after
            // its leading run counts, for either character.
            if (run < line.size()) {
                if (leader == '\t') {
                    ++tabLines;
                }
                else {
                    ++spaceLines;
                    minSpaceRun = std::min(minSpaceRun, static_cast<int>(run));
                }
            }
        }

        if (lineEnd == std::string_view::npos) {
            break;
        }
        lineStart = lineEnd + 1;
    }

    DetectedIndent result;
    if (tabLines == 0 && spaceLines == 0) {
        result.kind = DetectedIndentKind::Unknown;
        return result;
    }

    const std::size_t larger  = std::max(tabLines, spaceLines);
    const std::size_t smaller = std::min(tabLines, spaceLines);
    if (tabLines > 0 && spaceLines > 0 && smaller * 4 >= larger) {
        result.kind = DetectedIndentKind::Mixed;
        return result;
    }

    if (tabLines >= spaceLines) {
        result.kind = DetectedIndentKind::Tabs;
    }
    else {
        result.kind        = DetectedIndentKind::Spaces;
        result.spacesWidth = (minSpaceRun == INT_MAX) ? 4 : minSpaceRun;
    }
    return result;
}

} // namespace ned::editor
