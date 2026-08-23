#include "DiffPatch.h"

#include <string_view>

namespace ned::editor::vcs {

namespace {

    struct CountedRange {
        std::size_t start;
        std::size_t count;
    };

    // Parses one side's range out of a "@@ -old[,count] +new[,count] @@"
    // hunk header, starting the scan right after marker (" -" for the old
    // side, " +" for the new one) -- mirroring the bundled git plugin's own
    // parse-hunk-header, which reads both fields the same way for the
    // gutter/multibuffer builders. nullopt on anything malformed -- the
    // caller skips such a hunk rather than crashing, the established
    // degrade-don't-crash posture for parsing external tool output.
    std::optional<CountedRange> ParseCountedRange(std::string_view headerLine, std::string_view marker) {
        const std::size_t markerPos = headerLine.find(marker);
        if (markerPos == std::string_view::npos) {
            return std::nullopt;
        }
        std::size_t pos = markerPos + marker.size();

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
        return CountedRange{*start, count};
    }

    std::optional<CountedRange> ParseNewSideRange(std::string_view headerLine) {
        return ParseCountedRange(headerLine, " +");
    }

    std::optional<CountedRange> ParseOldSideRange(std::string_view headerLine) {
        return ParseCountedRange(headerLine, " -");
    }

    bool Covers(const CountedRange& range, std::size_t targetLine) {
        if (range.count == 0) {
            // Pure deletion -- matches both boundary lines; see
            // ExtractHunkPatch's own doc comment.
            return targetLine == range.start || targetLine == range.start + 1;
        }
        return targetLine >= range.start && targetLine < range.start + range.count;
    }

    // One line of diff output as [pos, contentEnd) plus where the next line
    // starts -- shared by ExtractHunkPatch and ParseDiffHunks, both of which
    // walk diffOutput one line at a time looking for the same two markers
    // ("diff --git"/"@@ ").
    struct Line {
        std::string_view text;
        std::size_t      nextPos;
    };

    Line LineAt(const std::string& diffOutput, std::size_t pos) {
        const std::size_t size       = diffOutput.size();
        const std::size_t eol        = diffOutput.find('\n', pos);
        const std::size_t contentEnd = (eol == std::string::npos) ? size : eol;
        return Line{std::string_view(diffOutput.data() + pos, contentEnd - pos),
                    (eol == std::string::npos) ? size : eol + 1};
    }

    // Pulls the post-change path out of a "diff --git a/<path> b/<path>"
    // header line -- the "b/" side, since that's the name a rename/copy
    // lands on. rfind (not find) so a path itself containing " a/" doesn't
    // fool the split; still a heuristic, not a full re-implementation of
    // git's own (rare) path-quoting rules, matching ParseStatus's own
    // documented "good enough, not exotic-filename-proof" scope elsewhere
    // in this subsystem. "" if the marker isn't found.
    std::string ParseGitDiffFilePath(std::string_view headerLine) {
        const std::size_t marker = headerLine.rfind(" b/");
        if (marker == std::string_view::npos) {
            return {};
        }
        return std::string(headerLine.substr(marker + 3));
    }

} // namespace

std::optional<std::string> ExtractHunkPatch(const std::string& diffOutput, std::size_t targetLine) {
    const std::size_t size = diffOutput.size();

    std::size_t headerStart = 0;
    std::size_t headerEnd   = std::string::npos; // npos: not yet captured for the current file block

    std::size_t pos = 0;
    while (pos < size) {
        const auto line = LineAt(diffOutput, pos);

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
            const auto bodyLine = LineAt(diffOutput, bodyEnd);
            if (bodyLine.text.starts_with("@@ ") || bodyLine.text.starts_with("diff --git")) {
                break;
            }
            bodyEnd = bodyLine.nextPos;
        }

        const std::optional<CountedRange> range = ParseNewSideRange(line.text);
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

std::vector<DiffHunkText> ParseDiffHunks(const std::string& diffOutput) {
    const std::size_t size = diffOutput.size();

    std::vector<DiffHunkText> hunks;
    std::string               currentFile;

    std::size_t pos = 0;
    while (pos < size) {
        const auto line = LineAt(diffOutput, pos);

        if (line.text.starts_with("diff --git")) {
            currentFile = ParseGitDiffFilePath(line.text);
            pos         = line.nextPos;
            continue;
        }

        if (!line.text.starts_with("@@ ")) {
            pos = line.nextPos;
            continue;
        }

        const std::size_t bodyStart = line.nextPos;
        std::size_t       bodyEnd   = bodyStart;
        while (bodyEnd < size) {
            const auto bodyLine = LineAt(diffOutput, bodyEnd);
            if (bodyLine.text.starts_with("@@ ") || bodyLine.text.starts_with("diff --git")) {
                break;
            }
            bodyEnd = bodyLine.nextPos;
        }

        // A hunk under a file header this couldn't parse (no "diff --git"
        // ever seen, or its "b/" path was malformed), or whose header is
        // malformed on either side, is skipped -- there's no sensible
        // source location to attribute it to, the same degrade-don't-crash
        // posture ExtractHunkPatch takes toward a malformed hunk header.
        const std::optional<CountedRange> oldRange = ParseOldSideRange(line.text);
        const std::optional<CountedRange> newRange = ParseNewSideRange(line.text);
        if (oldRange && newRange && !currentFile.empty()) {
            hunks.push_back(DiffHunkText{currentFile, std::string(line.text), diffOutput.substr(bodyStart, bodyEnd - bodyStart),
                                         oldRange->start, oldRange->count, newRange->start, newRange->count});
        }

        pos = bodyEnd;
    }

    return hunks;
}

} // namespace ned::editor::vcs
