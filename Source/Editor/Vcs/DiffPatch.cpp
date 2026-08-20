#include "DiffPatch.h"

#include <string_view>

namespace ned::editor::vcs {

namespace {

    struct NewSideRange {
        std::size_t start;
        std::size_t count;
    };

    // Parses the new-side range out of a "@@ -old[,count] +new[,count] @@"
    // hunk header -- only the "+new[,count]" half matters here (the old
    // side describes content this helper never touches), mirroring the
    // bundled git plugin's own parse-hunk-header, which reads the same two
    // fields for the gutter. nullopt on anything malformed -- the caller
    // skips such a hunk rather than crashing, the established
    // degrade-don't-crash posture for parsing external tool output.
    std::optional<NewSideRange> ParseNewSideRange(std::string_view headerLine) {
        const std::size_t plus = headerLine.find(" +");
        if (plus == std::string_view::npos) {
            return std::nullopt;
        }
        std::size_t pos = plus + 2;

        auto parseNumber = [&headerLine, &pos]() -> std::optional<std::size_t> {
            if (pos >= headerLine.size() || headerLine[pos] < '0' || headerLine[pos] > '9') {
                return std::nullopt;
            }
            std::size_t value = 0;
            while (pos < headerLine.size() && headerLine[pos] >= '0' && headerLine[pos] <= '9') {
                value = value * 10 + static_cast<std::size_t>(headerLine[pos] - '0');
                ++pos;
            }
            return value;
        };

        const std::optional<std::size_t> start = parseNumber();
        if (!start) {
            return std::nullopt;
        }
        std::size_t count = 1; // omitted count means 1, unified diff's own convention
        if (pos < headerLine.size() && headerLine[pos] == ',') {
            ++pos;
            const std::optional<std::size_t> parsedCount = parseNumber();
            if (!parsedCount) {
                return std::nullopt;
            }
            count = *parsedCount;
        }
        return NewSideRange{*start, count};
    }

    bool Covers(const NewSideRange& range, std::size_t targetLine) {
        if (range.count == 0) {
            // Pure deletion -- matches both boundary lines; see
            // ExtractHunkPatch's own doc comment.
            return targetLine == range.start || targetLine == range.start + 1;
        }
        return targetLine >= range.start && targetLine < range.start + range.count;
    }

} // namespace

std::optional<std::string> ExtractHunkPatch(const std::string& diffOutput, std::size_t targetLine) {
    const std::size_t size = diffOutput.size();

    std::size_t headerStart = 0;
    std::size_t headerEnd   = std::string::npos; // npos: not yet captured for the current file block

    auto lineAt = [&diffOutput, size](std::size_t pos) {
        const std::size_t eol = diffOutput.find('\n', pos);
        // [pos, contentEnd) is the line's text, nextPos the following line's start.
        struct Line {
            std::string_view text;
            std::size_t      nextPos;
        };
        const std::size_t contentEnd = (eol == std::string::npos) ? size : eol;
        return Line{std::string_view(diffOutput.data() + pos, contentEnd - pos),
                    (eol == std::string::npos) ? size : eol + 1};
    };

    std::size_t pos = 0;
    while (pos < size) {
        const auto line = lineAt(pos);

        if (line.text.starts_with("diff --git")) {
            headerStart = pos;
            headerEnd   = std::string::npos;
            pos         = line.nextPos;
            continue;
        }

        if (!line.text.starts_with("@@ ")) {
            pos = line.nextPos;
            continue;
        }

        // A hunk: header line at pos, body running until the next hunk
        // header, the next file's "diff --git", or EOF -- everything else
        // (' '/'+'/'-' content lines and the "\ No newline..." marker)
        // belongs to it.
        if (headerEnd == std::string::npos) {
            headerEnd = pos;
        }

        std::size_t bodyEnd = line.nextPos;
        while (bodyEnd < size) {
            const auto bodyLine = lineAt(bodyEnd);
            if (bodyLine.text.starts_with("@@ ") || bodyLine.text.starts_with("diff --git")) {
                break;
            }
            bodyEnd = bodyLine.nextPos;
        }

        const std::optional<NewSideRange> range = ParseNewSideRange(line.text);
        if (range && Covers(*range, targetLine)) {
            std::string patch = diffOutput.substr(headerStart, headerEnd - headerStart) + diffOutput.substr(pos, bodyEnd - pos);
            if (!patch.empty() && patch.back() != '\n') {
                patch += '\n'; // a patch whose final line lacks its newline is rejected as corrupt by git apply
            }
            return patch;
        }

        pos = bodyEnd;
    }

    return std::nullopt;
}

} // namespace ned::editor::vcs
