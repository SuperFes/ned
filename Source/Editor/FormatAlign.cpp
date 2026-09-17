#include "FormatAlign.h"

#include <algorithm>
#include <string>
#include <unordered_map>

#include "FormatRules.h"

namespace ned::editor {

namespace {

    bool IsAlignGapChar(char c) {
        return c == ' ' || c == '\t';
    }

    // Scans backward from `at` over IsAlignGapChar bytes only, never
    // crossing a newline -- the same "one line, no further" scope every
    // other pass in this file's own family already holds its own gap scan
    // to (FormatBracePlacement.h/FormatSpacing.h).
    std::size_t GapStartBefore(std::string_view text, std::size_t at) {
        std::size_t start = at;
        while (start > 0 && IsAlignGapChar(text[start - 1])) {
            --start;
        }
        return start;
    }

} // namespace

std::vector<FormatTextEdit> ComputeAlignEdits(std::string_view text, std::string_view languageKey,
                                              const std::vector<FormatCapture>& captures) {
    std::vector<FormatTextEdit> edits;

    // Bucket by capture name first -- an unconfigured or disabled name is
    // dropped before grouping, so the run-detection loop below never even
    // looks at a name nobody asked to align.
    std::unordered_map<std::string, std::vector<const FormatCapture*>> byName;
    for (const FormatCapture& capture : captures) {
        if (capture.startByte >= capture.endByte || capture.endByte > text.size()) {
            continue; // degenerate span -- never expected from a real query
        }
        const AlignRuleValue rule = AlignRuleFor(capture.name, languageKey);
        if (!rule.enabled || !*rule.enabled) {
            continue; // unconfigured -- no built-in default, nothing forced
        }
        byName[capture.name].push_back(&capture);
    }

    for (auto& [name, group] : byName) {
        std::sort(group.begin(), group.end(),
                  [](const FormatCapture* a, const FormatCapture* b) { return a->startByte < b->startByte; });

        std::size_t i = 0;
        while (i < group.size()) {
            // this file's own header comment: a run is a maximal sequence
            // where each next capture sits on the line immediately
            // following the previous one's own line, at the same leading
            // indent. `prevLineStart` tracks the start of whichever line is
            // currently "the previous one" as the run extends.
            std::size_t            prevLineStart = LineStartOf(text, group[i]->startByte);
            const std::string_view runIndent     = LineIndentOf(text, group[i]->startByte);
            std::size_t            j             = i + 1;
            while (j < group.size()) {
                const std::size_t thisLineEnd = text.find('\n', prevLineStart);
                if (thisLineEnd == std::string_view::npos) {
                    break; // last line in the file -- nothing can follow it
                }
                const std::size_t nextLineStart = thisLineEnd + 1;
                if (LineStartOf(text, group[j]->startByte) != nextLineStart) {
                    break; // not on the very next line -- a blank/other line intervenes
                }
                if (LineIndentOf(text, group[j]->startByte) != runIndent) {
                    break; // different nesting depth -- decline rather than guess
                }
                prevLineStart = nextLineStart;
                ++j;
            }

            if (j - i >= 2) { // a run of one has nothing to align against
                std::size_t target = 0;
                for (std::size_t k = i; k < j; ++k) {
                    const std::size_t lineStart = LineStartOf(text, group[k]->startByte);
                    target                      = std::max(target, group[k]->startByte - lineStart);
                }
                for (std::size_t k = i; k < j; ++k) {
                    const std::size_t lineStart = LineStartOf(text, group[k]->startByte);
                    const std::size_t gapStart  = GapStartBefore(text, group[k]->startByte);
                    const std::size_t column    = gapStart - lineStart;
                    // target >= this member's own full column (its startByte
                    // - lineStart), which is >= column (gapStart's own,
                    // always <= startByte's) -- so this never underflows. No
                    // floor of 1: for the run's own widest member, target ==
                    // its full column, so this always comes out equal to its
                    // own currentGapLength (0 included) -- flooring it here
                    // used to force a spurious space into that line whenever
                    // its anchor already touched the preceding token.
                    const std::size_t desiredGapLength = target - column;
                    const std::size_t currentGapLength = group[k]->startByte - gapStart;
                    if (currentGapLength == desiredGapLength) {
                        continue;
                    }
                    edits.push_back(FormatTextEdit{gapStart, group[k]->startByte, std::string(desiredGapLength, ' ')});
                }
            }

            i = j;
        }
    }

    std::sort(edits.begin(), edits.end(), [](const FormatTextEdit& a, const FormatTextEdit& b) { return a.start < b.start; });
    return edits;
}

} // namespace ned::editor
