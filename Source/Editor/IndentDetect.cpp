#include "IndentDetect.h"

#include <algorithm>
#include <array>
#include <climits>
#include <cstddef>
#include <mutex>
#include <vector>

namespace ned::editor {

namespace {

    std::mutex& DetectionMutex() {
        static std::mutex mutex;
        return mutex;
    }

    bool& DetectionStorage() {
        static bool enabled = true;
        return enabled;
    }

    // The largest candidate unit that at least 4 in 5 runs are a multiple of.
    std::optional<int> CommonUnit(const std::vector<int>& runs) {
        static constexpr std::array kCandidates{8, 4, 3, 2};
        for (const int unit : kCandidates) {
            const auto multiples = std::ranges::count_if(runs, [unit](int run) { return run % unit == 0; });
            if (!runs.empty() && multiples * 5 >= static_cast<std::ptrdiff_t>(runs.size()) * 4) {
                return unit;
            }
        }
        return std::nullopt;
    }

} // namespace

DetectedIndent DetectIndentStyle(std::string_view text) {
    std::size_t      tabLines    = 0;
    std::size_t      spaceLines  = 0;
    int              minSpaceRun = INT_MAX;
    std::vector<int> spaceRuns;

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
            const std::size_t contentStart = line.find_first_not_of(" \t\r");
            if (contentStart != std::string_view::npos && line[contentStart] != '*') {
                if (leader == '\t') {
                    ++tabLines;
                }
                else {
                    ++spaceLines;
                    minSpaceRun = std::min(minSpaceRun, static_cast<int>(run));
                    spaceRuns.push_back(static_cast<int>(run));
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
        const std::optional<int> unit = CommonUnit(spaceRuns);
        result.spacesWidthConfident   = unit.has_value();
        result.spacesWidth            = unit ? *unit : (minSpaceRun == INT_MAX ? 4 : minSpaceRun);
    }
    return result;
}

IndentOverride DetectedIndentOverride(std::string_view text) {
    const DetectedIndent detected = DetectIndentStyle(text);
    switch (detected.kind) {
        case DetectedIndentKind::Tabs:
            return IndentOverride{.useTabs = true};
        case DetectedIndentKind::Spaces:
            return IndentOverride{.useTabs = false,
                                  .width   = detected.spacesWidthConfident ? std::optional<int>(detected.spacesWidth)
                                                                           : std::nullopt};
        case DetectedIndentKind::Mixed:
        case DetectedIndentKind::Unknown:
            break;
    }
    return {};
}

void SetIndentDetection(bool enabled) {
    const std::lock_guard<std::mutex> lock(DetectionMutex());
    DetectionStorage() = enabled;
}

bool IndentDetection() {
    const std::lock_guard<std::mutex> lock(DetectionMutex());
    return DetectionStorage();
}

} // namespace ned::editor
