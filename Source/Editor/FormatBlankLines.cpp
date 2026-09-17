#include "FormatBlankLines.h"

#include <algorithm>

#include "FormatEdit.h"
#include "FormatRules.h"

namespace ned::editor {

namespace {

    bool IsBlankLineChar(char c) {
        return c == ' ' || c == '\t' || c == '\r';
    }

    // Walks upward from `lineStart` (the start of a known line -- here,
    // always the capture's own line) counting consecutive blank lines
    // immediately above it. Returns that count and, via `blankRegionStart`,
    // the byte offset the blank run begins at -- right after the nearest
    // non-blank line's own trailing '\n', or 0 if everything above is
    // blank (including "this is the very start of the file").
    int CountBlankLinesBefore(std::string_view text, std::size_t lineStart, std::size_t& blankRegionStart) {
        int         count  = 0;
        std::size_t cursor = lineStart;
        while (cursor > 0) {
            // text[cursor - 1] is the '\n' terminating the line just above `cursor`.
            const std::size_t      prevLineStart = LineStartOf(text, cursor - 1);
            const std::string_view prevLine      = text.substr(prevLineStart, (cursor - 1) - prevLineStart);
            if (!std::all_of(prevLine.begin(), prevLine.end(), IsBlankLineChar)) {
                break;
            }
            ++count;
            cursor = prevLineStart;
        }
        blankRegionStart = cursor;
        return count;
    }

} // namespace

std::vector<FormatTextEdit> ComputeBlankLineEdits(std::string_view text, std::string_view languageKey,
                                                  const std::vector<FormatCapture>& captures) {
    std::vector<FormatTextEdit> edits;

    for (const FormatCapture& capture : captures) {
        if (capture.startByte >= capture.endByte || capture.endByte > text.size()) {
            continue;
        }
        const BlankRuleValue rule = BlankRuleFor(capture.name, languageKey);
        if (!rule.minBefore && !rule.maxBefore) {
            continue; // unconfigured -- no built-in default, nothing forced
        }

        const std::size_t lineStart = LineStartOf(text, capture.startByte);
        std::size_t       blankRegionStart{};
        const int         blankCount = CountBlankLinesBefore(text, lineStart, blankRegionStart);

        int desired = blankCount;
        if (rule.minBefore && !capture.isFirst) {
            desired = std::max(desired, *rule.minBefore);
        }
        if (rule.maxBefore) {
            desired = std::min(desired, *rule.maxBefore);
        }
        desired = std::max(desired, 0); // defend against a misconfigured negative maxBefore

        if (desired == blankCount) {
            continue;
        }

        std::string desiredGap(static_cast<std::size_t>(desired), '\n');
        desiredGap += text.substr(lineStart, capture.startByte - lineStart); // this line's own leading indent
        edits.push_back(FormatTextEdit{blankRegionStart, capture.startByte, std::move(desiredGap)});
    }

    std::sort(edits.begin(), edits.end(), [](const FormatTextEdit& a, const FormatTextEdit& b) { return a.start < b.start; });
    return edits;
}

} // namespace ned::editor
