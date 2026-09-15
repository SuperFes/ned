#include "FormatBracePlacement.h"

#include <algorithm>

#include "FormatRules.h"
#include "Indent.h"
#include "IndentStyle.h"

namespace ned::editor {

namespace {

    bool IsFormatWhitespace(char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r';
    }

    // The literal leading-whitespace substring of the line containing byte
    // offset `at` -- reused verbatim (not recomputed from a column) so a
    // mixed tabs/spaces header's own indent survives untouched, matching
    // NextLineIndented's own "one level deeper than whatever's already
    // there" contract rather than a from-scratch column recomputation.
    std::string_view LineIndentOf(std::string_view text, std::size_t at) {
        const std::size_t lineStart =
            (at == 0) ? 0 : [&] {
                const std::size_t found = text.rfind('\n', at - 1);
                return found == std::string_view::npos ? std::size_t{0} : found + 1;
            }();
        std::size_t indentEnd = lineStart;
        while (indentEnd < text.size() && (text[indentEnd] == ' ' || text[indentEnd] == '\t')) {
            ++indentEnd;
        }
        return text.substr(lineStart, indentEnd - lineStart);
    }

} // namespace

std::vector<FormatTextEdit> ComputeBracePlacementEdits(std::string_view text, std::string_view languageKey,
                                                        const std::vector<FormatCapture>& captures) {
    std::vector<FormatTextEdit> edits;
    const IndentStyle           style = EffectiveIndentStyle(std::string(languageKey) + "-mode");

    for (const FormatCapture& capture : captures) {
        if (capture.startByte == 0 || capture.startByte > text.size()) {
            continue; // nothing could precede a capture at offset 0
        }
        const BreakRuleValue rule = BreakRuleFor(capture.name, languageKey);
        if (!rule.placement) {
            continue; // unconfigured -- no built-in default, nothing forced
        }

        // The header's own end: the last non-whitespace byte before the
        // capture's start.
        std::size_t headerEnd = capture.startByte;
        while (headerEnd > 0 && IsFormatWhitespace(text[headerEnd - 1])) {
            --headerEnd;
        }
        if (headerEnd == 0) {
            continue; // a brace with nothing at all before it
        }

        const std::string_view headerIndent = LineIndentOf(text, headerEnd - 1);

        std::string desiredGap;
        switch (*rule.placement) {
            case BracePlacement::SameLine:
                desiredGap = " ";
                break;
            case BracePlacement::NextLine:
                desiredGap = "\n";
                desiredGap += headerIndent;
                break;
            case BracePlacement::NextLineIndented:
                desiredGap = "\n";
                desiredGap += headerIndent;
                desiredGap += IndentString(style.width, style);
                break;
        }

        const std::string_view currentGap = text.substr(headerEnd, capture.startByte - headerEnd);
        if (currentGap != desiredGap) {
            edits.push_back(FormatTextEdit{headerEnd, capture.startByte, std::move(desiredGap)});
        }
    }

    std::sort(edits.begin(), edits.end(), [](const FormatTextEdit& a, const FormatTextEdit& b) { return a.start < b.start; });
    return edits;
}

void ApplyFormatTextEdits(text::Buffer& buffer, std::vector<FormatTextEdit> edits) {
    if (edits.empty()) {
        return;
    }
    std::sort(edits.begin(), edits.end(), [](const FormatTextEdit& a, const FormatTextEdit& b) { return a.start < b.start; });

    buffer.BeginUndoGroup();
    for (auto it = edits.rbegin(); it != edits.rend(); ++it) {
        buffer.DeleteRange(it->start, it->end - it->start); // DeleteRange's 2nd argument is a LENGTH, not an end offset
        buffer.InsertAt(it->start, it->text);
    }
    buffer.EndUndoGroup();
}

} // namespace ned::editor
