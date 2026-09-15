#include "FormatSpacing.h"

#include <algorithm>

#include "FormatRules.h"

namespace ned::editor {

namespace {

    // A horizontal (space/tab-only) whitespace run, plus whether the byte
    // immediately outside it (the direction the caller scanned from) is a
    // newline -- crossesLine means "there is no adjustable run here at all",
    // never "the run is empty," and the caller must leave it untouched.
    struct Gap {
        std::size_t start;
        std::size_t end;
        bool        crossesLine;
    };

    // The horizontal run ending exactly at `at` (scanning backward).
    Gap HorizontalGapBefore(std::string_view text, std::size_t at) {
        if (at == 0 || text[at - 1] == '\n' || text[at - 1] == '\r') {
            return {at, at, true};
        }
        std::size_t start = at;
        while (start > 0 && (text[start - 1] == ' ' || text[start - 1] == '\t')) {
            --start;
        }
        return {start, at, false};
    }

    // The horizontal run starting exactly at `at` (scanning forward).
    Gap HorizontalGapAfter(std::string_view text, std::size_t at) {
        if (at >= text.size() || text[at] == '\n' || text[at] == '\r') {
            return {at, at, true};
        }
        std::size_t end = at;
        while (end < text.size() && (text[end] == ' ' || text[end] == '\t')) {
            ++end;
        }
        return {at, end, false};
    }

    void EmitIfChanged(std::vector<FormatTextEdit>& edits, std::string_view text, const Gap& gap, bool wantSpace) {
        if (gap.crossesLine) {
            return;
        }
        const std::string_view desired = wantSpace ? " " : "";
        if (text.substr(gap.start, gap.end - gap.start) != desired) {
            edits.push_back(FormatTextEdit{gap.start, gap.end, std::string(desired)});
        }
    }

} // namespace

std::vector<FormatTextEdit> ComputeSpaceEdits(std::string_view text, std::string_view languageKey,
                                              const std::vector<FormatCapture>& captures) {
    std::vector<FormatTextEdit> edits;

    for (const FormatCapture& capture : captures) {
        if (capture.startByte > text.size() || capture.endByte > text.size() || capture.startByte >= capture.endByte) {
            continue; // malformed capture -- defensive, never expected from a real query
        }
        const SpaceRuleValue rule = SpaceRuleFor(capture.name, languageKey);
        if (!rule.before && !rule.after && !rule.within) {
            continue; // unconfigured -- no built-in default, nothing forced
        }

        if (rule.before) {
            EmitIfChanged(edits, text, HorizontalGapBefore(text, capture.startByte), *rule.before);
        }
        if (rule.after) {
            EmitIfChanged(edits, text, HorizontalGapAfter(text, capture.endByte), *rule.after);
        }
        if (rule.within && capture.endByte - capture.startByte >= 2) {
            const Gap openGap  = HorizontalGapAfter(text, capture.startByte + 1);
            const Gap closeGap = HorizontalGapBefore(text, capture.endByte - 1);
            EmitIfChanged(edits, text, openGap, *rule.within);
            // A genuinely empty pair ("()", nothing between the delimiters)
            // has openGap and closeGap sitting at the exact same position --
            // emitting both would double-insert ("(  )" instead of "( )").
            // Emit the second only when it's a real, distinct gap.
            if (openGap.start != closeGap.start || openGap.end != closeGap.end) {
                EmitIfChanged(edits, text, closeGap, *rule.within);
            }
        }
    }

    std::sort(edits.begin(), edits.end(), [](const FormatTextEdit& a, const FormatTextEdit& b) { return a.start < b.start; });
    return edits;
}

} // namespace ned::editor
