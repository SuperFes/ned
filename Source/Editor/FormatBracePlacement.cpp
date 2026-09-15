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

    // keyword-delimiter-captures follow-up: whether gluing two delimiter
    // tokens directly together would fuse them into one word -- true for
    // any ASCII letter/digit/underscore, which is every byte a keyword
    // token ("do", "end") can end or start with. A brace/paren is never a
    // word byte, so this is always false for every pre-Lua capture.
    bool IsWordByte(char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
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
    // bash-format-revisit follow-up: the INVERSE hazard from Go's own --
    // here SameLine specifically is the dangerous value, every other
    // placement is safe. `do`/`then` are bash reserved words that must be
    // preceded by a real statement TERMINATOR (a semicolon or a newline),
    // never merely whitespace -- confirmed live with a real `bash -n`:
    // "while true do" and "if true then" (SameLine's own plain-space gap)
    // are hard syntax errors, while "while true\ndo"/"if true\nthen"
    // (NextLine's own newline gap, which IS a valid terminator) parse
    // fine. This is real for while/until/for/select's own "do" and
    // if_statement's own "then" -- NOT for case_statement's own "in" or a
    // C-style for-loop's own "do" (both have an OPTIONAL terminator in
    // the grammar, confirmed live SameLine is fine for either) -- but
    // brace.control captures do_group/if_statement/case_statement all
    // under one shared name with no per-instance signal available here,
    // so this declines SameLine for the capture NAME as a whole rather
    // than risk corrupting the while/until/for/if shapes that need it.
    // brace.function is unaffected (bash's function bodies are real
    // braces, needing no terminator at all, confirmed live SameLine works
    // there) -- Go's own guard didn't need a capture-name parameter
    // because its hazard was language-wide; this one only exists on
    // brace.control, so the added parameter is real, not speculative.
    bool PlacementUnsafeForLanguage(BracePlacement placement, std::string_view languageKey, std::string_view captureName) {
        if (languageKey == "go") {
            return placement != BracePlacement::SameLine;
        }
        if (languageKey == "bash" && captureName == "brace.control") {
            return placement == BracePlacement::SameLine;
        }
        return false;
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
        if (capture.openLength == 0 || capture.closeLength == 0 ||
            capture.openLength + capture.closeLength > capture.endByte - capture.startByte) {
            continue; // degenerate delimiter lengths -- never expected from a real .open/.close pair
        }
        BreakRuleValue rule = BreakRuleFor(capture.name, languageKey);
        if (rule.placement && PlacementUnsafeForLanguage(*rule.placement, languageKey, capture.name)) {
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

        // keyword-delimiter-captures follow-up: the open/close TOKEN text
        // itself, not just its length -- "{"/"}"  for every capture before
        // Lua's, "do"/"end" or "then"/"end" for one of Lua's own paired
        // captures. Read once here and reused by both collapse-empty and
        // collapse-simple below.
        const std::string_view openText  = text.substr(capture.startByte, capture.openLength);
        const std::string_view closeText = text.substr(capture.endByte - capture.closeLength, capture.closeLength);

        // collapse-empty: purely textual and unambiguous, no tree needed --
        // whitespace-only content between the two delimiter tokens is empty
        // regardless of language. Owns the WHOLE capture span in one edit
        // (open delimiter through close), which is why it's mutually
        // exclusive with the closer-repositioning step below for the same
        // capture: an empty "{\n}" body's closer is "alone on its own
        // line" too, and letting both steps touch it would emit two
        // overlapping edits.
        const std::string_view interior =
            text.substr(capture.startByte + capture.openLength,
                        capture.endByte - capture.startByte - capture.openLength - capture.closeLength);
        const bool isEmpty = std::all_of(interior.begin(), interior.end(), IsFormatWhitespace);
        if (rule.collapseEmpty && isEmpty) {
            std::string desired(openText);
            if (*rule.collapseEmpty) {
                // A single-character delimiter glues with nothing between
                // ("{}"); a keyword delimiter needs a real separator or
                // the two tokens fuse into one identifier ("doend" is not
                // "do"+"end") -- inserted whenever both sides are
                // word-constituent bytes, a general rule rather than a
                // per-language one.
                if (!openText.empty() && !closeText.empty() && IsWordByte(openText.back()) && IsWordByte(closeText.front())) {
                    desired += ' ';
                }
                desired += closeText; // glue: "{}" / "do end"
            }
            else {
                desired += "\n";
                desired += ClosingIndentFor(rule.placement, headerIndent, style);
                desired += closeText;
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
            const std::string_view interior =
                text.substr(capture.startByte + capture.openLength,
                            capture.endByte - capture.startByte - capture.openLength - capture.closeLength);
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
                    std::string desired(openText);
                    desired += " ";
                    desired += trimmed;
                    desired += " ";
                    desired += closeText;
                    if (wholeSpan != desired) {
                        edits.push_back(FormatTextEdit{capture.startByte, capture.endByte, std::move(desired)});
                    }
                }
            }
            else if (wholeSpan.find('\n') == std::string_view::npos) {
                std::string bodyIndent(headerIndent);
                bodyIndent += IndentString(style.width, style);
                std::string desired(openText);
                desired += "\n";
                desired += bodyIndent;
                desired += trimmed;
                desired += "\n";
                desired += ClosingIndentFor(rule.placement, headerIndent, style);
                desired += closeText;
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
            // keyword-delimiter-captures follow-up: the closer TOKEN's own
            // start, not just "one byte before the capture ends" -- a
            // single-char delimiter has the two coincide (closerPos ==
            // closerTokenStart), a multi-byte one ("end") does not, and
            // repositioning must leave the token itself untouched either
            // way.
            const std::size_t closerTokenStart = capture.endByte - capture.closeLength;
            const std::size_t closerLineStart  = [&] {
                const std::size_t found = text.rfind('\n', closerTokenStart == 0 ? 0 : closerTokenStart - 1);
                return found == std::string_view::npos ? std::size_t{0} : found + 1;
            }();
            const std::string_view  beforeCloser = text.substr(closerLineStart, closerTokenStart - closerLineStart);
            const bool              closerIsAloneOnItsLine =
                std::all_of(beforeCloser.begin(), beforeCloser.end(), [](char c) { return c == ' ' || c == '\t'; });
            if (closerIsAloneOnItsLine) {
                std::string desiredCloserIndent = ClosingIndentFor(rule.placement, headerIndent, style);
                if (beforeCloser != desiredCloserIndent) {
                    edits.push_back(FormatTextEdit{closerLineStart, closerTokenStart, std::move(desiredCloserIndent)});
                }
            }
        }
    }

    std::sort(edits.begin(), edits.end(), [](const FormatTextEdit& a, const FormatTextEdit& b) { return a.start < b.start; });
    return edits;
}

} // namespace ned::editor
