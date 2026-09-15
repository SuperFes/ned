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

    // go-language-pilot follow-up: Go's grammar performs automatic
    // semicolon insertion after a `)` token at end-of-line (the Go spec's
    // own rule -- confirmed live with a real `go build`, not assumed: a
    // function header on its own line followed by `{` on the next fails to
    // compile with "syntax error: unexpected semicolon or newline before
    // {", because the inserted semicolon after `)` terminates the
    // declaration before the brace is ever reached). Every OTHER
    // ASI-adjacent language already in this template is unaffected --
    // JavaScript's own ASI does not fire after `)`, so `function f()\n{`
    // parses (and compiles) fine there, verified against the live grammar
    // the same way. `:placement` values other than SameLine are therefore
    // not merely a style preference for Go, they are a correctness hazard:
    // a project's SHARED `:break` rule (the whole point of the
    // language-scoped-override design) would silently break every Go
    // buffer it touches. Neutralized here rather than left to the config
    // author to avoid -- the same "decline rather than risk corruption"
    // precedent Text/DiskSpace.h's save-guard and collapse-simple's
    // multi-line decline both already set.
    bool PlacementUnsafeForLanguage(BracePlacement placement, std::string_view languageKey) {
        return placement != BracePlacement::SameLine && languageKey == "go";
    }

    // Where this construct's closing delimiter belongs, for a given
    // placement -- shared between the "reposition an existing multi-line
    // closer" step and collapse-empty's "force expand" step, so the two
    // always agree on where the closer goes. Every placement but
    // NextLineIndented aligns the closer with the header's own indent
    // (SameLine/NextLine/no-placement-configured-at-all alike); only
    // GNU/Whitesmiths adds the extra level (see this file's own header
    // comment on why).
    std::string ClosingIndentFor(std::optional<BracePlacement> placement, std::string_view headerIndent,
                                 const IndentStyle& style) {
        std::string indent(headerIndent);
        if (placement == BracePlacement::NextLineIndented) {
            indent += IndentString(style.width, style);
        }
        return indent;
    }

} // namespace

std::vector<FormatTextEdit> ComputeBracePlacementEdits(std::string_view text, std::string_view languageKey,
                                                        const std::vector<FormatCapture>& captures) {
    std::vector<FormatTextEdit> edits;
    const IndentStyle           style = EffectiveIndentStyle(std::string(languageKey) + "-mode");

    for (const FormatCapture& capture : captures) {
        if (capture.startByte == 0 || capture.startByte >= capture.endByte || capture.endByte > text.size()) {
            continue; // nothing could precede a capture at offset 0; a degenerate span is never expected from a real query
        }
        BreakRuleValue rule = BreakRuleFor(capture.name, languageKey);
        if (rule.placement && PlacementUnsafeForLanguage(*rule.placement, languageKey)) {
            rule.placement.reset(); // see PlacementUnsafeForLanguage's own comment
        }
        if (!rule.placement && !rule.collapseEmpty && !rule.collapseSimple) {
            continue; // unconfigured -- no built-in default, nothing forced
        }

        // The header's own end: the last non-whitespace byte before the
        // capture's start. Needed by both :placement and collapse-empty's
        // force-expand path (which needs to know where the closer belongs
        // even with no :placement configured at all), so computed whenever
        // either field is set.
        std::size_t headerEnd = capture.startByte;
        while (headerEnd > 0 && IsFormatWhitespace(text[headerEnd - 1])) {
            --headerEnd;
        }
        if (headerEnd == 0) {
            continue; // a brace with nothing at all before it
        }

        const std::string_view headerIndent = LineIndentOf(text, headerEnd - 1);

        if (rule.placement) {
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

        // collapse-empty: purely textual and unambiguous, no tree needed --
        // whitespace-only content between the two delimiter bytes is empty
        // regardless of language. Owns the WHOLE capture span in one edit
        // (open delimiter through close), which is why it's mutually
        // exclusive with the closer-repositioning step below for the same
        // capture: an empty "{\n}" body's closer is "alone on its own
        // line" too, and letting both steps touch it would emit two
        // overlapping edits.
        const std::string_view interior = text.substr(capture.startByte + 1, capture.endByte - capture.startByte - 2);
        const bool              isEmpty  = std::all_of(interior.begin(), interior.end(), IsFormatWhitespace);
        if (rule.collapseEmpty && isEmpty) {
            std::string desired(1, text[capture.startByte]);
            if (*rule.collapseEmpty) {
                desired += text[capture.endByte - 1]; // glue: "{}"
            }
            else {
                desired += "\n";
                desired += ClosingIndentFor(rule.placement, headerIndent, style);
                desired += text[capture.endByte - 1];
            }
            const std::string_view current = text.substr(capture.startByte, capture.endByte - capture.startByte);
            if (current != desired) {
                edits.push_back(FormatTextEdit{capture.startByte, capture.endByte, std::move(desired)});
            }
            continue; // this capture's body is fully handled; skip the closer-repositioning step below
        }

        // collapse-simple: capture.isSimple is a structural fact (exactly
        // one top-level statement, whatever it itself contains) set by
        // Mode.cpp's "<name>.simple" marker correlation -- never guessed
        // from text. isEmpty/isSimple are mutually exclusive by
        // construction (the marker query requires "exactly one" child, an
        // empty body has zero), so this never fires for a capture
        // collapse-empty already handled above.
        //
        // Deliberately conservative in both directions: collapsing a
        // statement that ALREADY spans multiple lines (a long call, a
        // multi-line lambda) is declined rather than joining lines that
        // might be meaningfully broken (a comment, a string) -- and
        // force-expanding is declined when the body isn't currently a
        // single physical line, so this never re-flows something already
        // spread across lines in some other shape.
        if (rule.collapseSimple && capture.isSimple) {
            const std::string_view interior = text.substr(capture.startByte + 1, capture.endByte - capture.startByte - 2);
            std::size_t            trimStart = 0;
            while (trimStart < interior.size() && IsFormatWhitespace(interior[trimStart])) {
                ++trimStart;
            }
            std::size_t trimEnd = interior.size();
            while (trimEnd > trimStart && IsFormatWhitespace(interior[trimEnd - 1])) {
                --trimEnd;
            }
            const std::string_view trimmed  = interior.substr(trimStart, trimEnd - trimStart);
            const std::string_view wholeSpan = text.substr(capture.startByte, capture.endByte - capture.startByte);

            if (*rule.collapseSimple) {
                if (trimmed.find('\n') == std::string_view::npos) {
                    std::string desired(1, text[capture.startByte]);
                    desired += " ";
                    desired += trimmed;
                    desired += " ";
                    desired += text[capture.endByte - 1];
                    if (wholeSpan != desired) {
                        edits.push_back(FormatTextEdit{capture.startByte, capture.endByte, std::move(desired)});
                    }
                }
            }
            else if (wholeSpan.find('\n') == std::string_view::npos) {
                std::string bodyIndent(headerIndent);
                bodyIndent += IndentString(style.width, style);
                std::string desired(1, text[capture.startByte]);
                desired += "\n";
                desired += bodyIndent;
                desired += trimmed;
                desired += "\n";
                desired += ClosingIndentFor(rule.placement, headerIndent, style);
                desired += text[capture.endByte - 1];
                if (wholeSpan != desired) {
                    edits.push_back(FormatTextEdit{capture.startByte, capture.endByte, std::move(desired)});
                }
            }
            continue; // this capture's body is fully handled either way; skip the closer-repositioning step below
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
        // a collapsed one-line body ("{ return 1; }", collapse-simple's
        // territory, not this one) is left alone.
        if (rule.placement == BracePlacement::NextLineIndented) {
            const std::size_t closerPos       = capture.endByte - 1;
            const std::size_t closerLineStart = [&] {
                const std::size_t found = text.rfind('\n', closerPos == 0 ? 0 : closerPos - 1);
                return found == std::string_view::npos ? std::size_t{0} : found + 1;
            }();
            const std::string_view beforeCloser = text.substr(closerLineStart, closerPos - closerLineStart);
            const bool              closerIsAloneOnItsLine =
                std::all_of(beforeCloser.begin(), beforeCloser.end(), [](char c) { return c == ' ' || c == '\t'; });
            if (closerIsAloneOnItsLine) {
                std::string desiredCloserIndent = ClosingIndentFor(rule.placement, headerIndent, style);
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
