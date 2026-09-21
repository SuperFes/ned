#include "SearchEverywherePreview.h"

#include <algorithm>
#include <limits>
#include <string_view>

#include "Text/Utf8.h"

namespace ned::editor {

namespace {

    // U+25B8, the disclosure/run triangle this codebase already marks a
    // located line with (a multibuffer excerpt header, a VCS hunk header).
    constexpr std::string_view kTargetMarker  = "▸ ";
    constexpr std::string_view kPlainMarker   = "  ";
    constexpr std::size_t      kMarkerColumns = 2; // one glyph + one space, in cells not bytes

    // Tabs advance to the next multiple of tabWidth rather than emitting a
    // fixed run, so a line mixing tabs and spaces keeps the columns it has
    // on screen. A tabWidth of 0 would never advance -- treat it as 1.
    std::string ExpandTabs(std::string_view line, std::size_t tabWidth) {
        const std::size_t width = std::max<std::size_t>(tabWidth, 1);
        std::string       expanded;
        expanded.reserve(line.size());
        std::size_t column = 0;
        for (std::size_t i = 0; i < line.size();) {
            if (line[i] == '\t') {
                const std::size_t spaces = width - (column % width);
                expanded.append(spaces, ' ');
                column += spaces;
                ++i;
                continue;
            }
            const std::size_t next = text::NextCodepointBoundary(line, i);
            expanded.append(line, i, next - i);
            ++column; // one cell per codepoint, ListPopup's own approximation
            i = next;
        }
        return expanded;
    }

    std::string_view TrimTrailingWhitespace(std::string_view line) {
        while (!line.empty() && (line.back() == ' ' || line.back() == '\t' || line.back() == '\r')) {
            line.remove_suffix(1);
        }
        return line;
    }

    // Leading spaces only -- tabs are already expanded by the time this runs,
    // and a line of nothing but whitespace was already trimmed to empty.
    std::size_t LeadingSpaces(std::string_view line) {
        const std::size_t first = line.find_first_not_of(' ');
        return first == std::string_view::npos ? line.size() : first;
    }

    std::string TruncateToColumns(std::string_view line, std::size_t columns) {
        std::size_t pos   = 0;
        std::size_t count = 0;
        while (pos < line.size() && count < columns) {
            pos = text::NextCodepointBoundary(line, pos);
            ++count;
        }
        return std::string(line.substr(0, pos));
    }

} // namespace

std::vector<std::string> FormatSearchEverywherePreview(const std::vector<std::string>& rawLines,
                                                       std::optional<std::size_t>      targetIndex,
                                                       std::size_t                     tabWidth) {
    std::vector<std::string> expanded;
    expanded.reserve(rawLines.size());
    for (const std::string& raw : rawLines) {
        expanded.push_back(ExpandTabs(TrimTrailingWhitespace(raw), tabWidth));
    }

    // A window that is entirely blank has nothing to show -- returning four
    // empty rows would open a footer that reads as a rendering bug rather
    // than as "this is what's there".
    if (std::all_of(expanded.begin(), expanded.end(), [](const std::string& line) { return line.empty(); })) {
        return {};
    }

    // Blank lines carry no indentation to have in common, so they neither
    // constrain the strip nor get stripped themselves.
    std::size_t common = std::numeric_limits<std::size_t>::max();
    for (const std::string& line : expanded) {
        if (!line.empty()) {
            common = std::min(common, LeadingSpaces(line));
        }
    }
    if (common == std::numeric_limits<std::size_t>::max()) {
        common = 0;
    }

    const std::size_t columns =
        kSearchEverywherePreviewMaxColumns - (targetIndex ? kMarkerColumns : 0);

    std::vector<std::string> lines;
    lines.reserve(expanded.size());
    for (std::size_t i = 0; i < expanded.size(); ++i) {
        std::string_view body = expanded[i];
        body.remove_prefix(std::min(common, body.size()));
        std::string line;
        // A blank line gets no padding -- trailing whitespace on a row that
        // shows nothing is invisible either way, and leaving it off keeps
        // the formatted window comparable to the source lines it came from.
        if (targetIndex && !body.empty()) {
            line = (i == *targetIndex) ? std::string(kTargetMarker) : std::string(kPlainMarker);
        }
        line += TruncateToColumns(body, columns);
        lines.push_back(std::move(line));
    }
    return lines;
}

} // namespace ned::editor
