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

        // NextLineIndented is the one placement whose closing delimiter does
        // NOT align with the header's own indent (SameLine/NextLine's
        // closer already matches it, since that's the ordinary indenter's
        // own convention for where a block's closer belongs) -- GNU/
        // Whitesmiths instead aligns the closer with the OPENING
        // delimiter's own (deeper) column. Left unhandled, the pair ends up
        // structurally mismatched: the open brace one level deeper than the
        // header, the close brace still at the header's own indent (found
        // via a live probe, not assumed -- see [[project-format-rules-per-language-engine]]).
        // Only applied when the closer is the FIRST thing on its own line --
        // a collapsed one-line body ("int f() {}"/"{ return 1; }") is left
        // alone, matching the same "nothing inside the body is ever
        // touched" contract as everywhere else in this function; that's
        // collapse-empty/collapse-simple's territory, not this one.
        if (*rule.placement == BracePlacement::NextLineIndented && capture.endByte > capture.startByte + 1) {
            const std::size_t closerPos       = capture.endByte - 1;
            const std::size_t closerLineStart = [&] {
                const std::size_t found = text.rfind('\n', closerPos == 0 ? 0 : closerPos - 1);
                return found == std::string_view::npos ? std::size_t{0} : found + 1;
            }();
            const std::string_view beforeCloser = text.substr(closerLineStart, closerPos - closerLineStart);
            const bool              closerIsAloneOnItsLine =
                std::all_of(beforeCloser.begin(), beforeCloser.end(), [](char c) { return c == ' ' || c == '\t'; });
            if (closerIsAloneOnItsLine) {
                std::string desiredCloserIndent(headerIndent);
                desiredCloserIndent += IndentString(style.width, style);
                if (beforeCloser != desiredCloserIndent) {
                    edits.push_back(FormatTextEdit{closerLineStart, closerPos, std::move(desiredCloserIndent)});
                }
            }
        }
    }

    std::sort(edits.begin(), edits.end(), [](const FormatTextEdit& a, const FormatTextEdit& b) { return a.start < b.start; });
    return edits;
}

} // namespace ned::editor
