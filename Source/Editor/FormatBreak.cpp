#include "FormatBreak.h"

#include <algorithm>
#include <string>

#include "FormatRules.h"

namespace ned::editor {

namespace {

    // Go's automatic semicolon insertion terminates the statement at the
    // newline after `}`, so a break before `else` is not a style choice but
    // "syntax error: unexpected else" -- the same hazard
    // FormatBracePlacement.cpp's own PlacementUnsafeForLanguage already
    // refuses Go for. go/format.janet declares no control.keyword capture at
    // all, so this is the second line of defence rather than the only one.
    bool BreakUnsafeForLanguage(std::string_view languageKey, bool wantBreak) {
        return wantBreak && languageKey == "go";
    }

    bool IsBreakWhitespace(char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r';
    }

    // The leading whitespace of the line `at` sits on.
    std::string_view LineIndentAt(std::string_view text, std::size_t at) {
        const std::size_t lineStart = text.rfind('\n', at) == std::string_view::npos ? 0 : text.rfind('\n', at) + 1;
        std::size_t       end       = lineStart;
        while (end < text.size() && (text[end] == ' ' || text[end] == '\t')) {
            ++end;
        }
        return text.substr(lineStart, end - lineStart);
    }

    // Whether anything but whitespace precedes `at` on its own line -- i.e.
    // whether there is a real token whose column a break-before can inherit.
    bool HasContentBefore(std::string_view text, std::size_t at) {
        const std::size_t lineStart = text.rfind('\n', at) == std::string_view::npos ? 0 : text.rfind('\n', at) + 1;
        return std::any_of(text.begin() + static_cast<std::ptrdiff_t>(lineStart),
                           text.begin() + static_cast<std::ptrdiff_t>(at),
                           [](char c) { return !IsBreakWhitespace(c); });
    }

    // A gap is only ours to rewrite when it is pure whitespace: a comment
    // sitting between the two tokens has no correct placement either way.
    bool GapIsWhitespace(std::string_view text, std::size_t start, std::size_t end) {
        return std::all_of(text.begin() + static_cast<std::ptrdiff_t>(start),
                           text.begin() + static_cast<std::ptrdiff_t>(end), IsBreakWhitespace);
    }

    bool GapCrossesLine(std::string_view text, std::size_t start, std::size_t end) {
        return text.substr(start, end - start).find('\n') != std::string_view::npos;
    }

    void EmitIfChanged(std::vector<FormatTextEdit>& edits, std::string_view text, std::size_t start, std::size_t end,
                       std::string desired) {
        if (text.substr(start, end - start) != desired) {
            edits.push_back(FormatTextEdit{start, end, std::move(desired)});
        }
    }

    // Whether a captured construct (a body's closing brace, typically) ends
    // exactly at `at` -- proof the byte before a gap is a real token, not the
    // tail of a comment, so joining across the gap cannot swallow anything.
    bool CapturedTokenEndsAt(const std::vector<FormatCapture>& captures, std::size_t at) {
        return std::ranges::any_of(captures, [at](const FormatCapture& capture) {
            return capture.endByte == at && capture.name != "comment";
        });
    }

    // The whitespace run ending at the capture's own first byte, and what
    // should replace it.
    void BreakBefore(std::vector<FormatTextEdit>& edits, std::string_view text, std::size_t at, bool wantBreak,
                     const std::vector<FormatCapture>& captures) {
        std::size_t start = at;
        while (start > 0 && IsBreakWhitespace(text[start - 1])) {
            --start;
        }
        if (start == at && wantBreak && at == 0) {
            return; // nothing before it at all
        }
        if (!GapIsWhitespace(text, start, at)) {
            return;
        }
        if (!wantBreak) {
            if (GapCrossesLine(text, start, at) && !CapturedTokenEndsAt(captures, start)) {
                return; // see the header comment: only a captured closer is safe to join onto
            }
            EmitIfChanged(edits, text, start, at, start == 0 ? "" : " ");
            return;
        }
        if (start == 0 || !HasContentBefore(text, start)) {
            return; // no closer's column to inherit -- see the header comment
        }
        EmitIfChanged(edits, text, start, at, "\n" + std::string(LineIndentAt(text, start - 1)));
    }

    void BreakAfter(std::vector<FormatTextEdit>& edits, std::string_view text, std::size_t at, bool wantBreak) {
        std::size_t end = at;
        while (end < text.size() && IsBreakWhitespace(text[end])) {
            ++end;
        }
        if (!GapIsWhitespace(text, at, end)) {
            return;
        }
        if (end >= text.size()) {
            return; // end of document: nothing to separate from
        }
        if (!wantBreak) {
            if (GapCrossesLine(text, at, end)) {
                return; // see the header comment
            }
            EmitIfChanged(edits, text, at, end, " ");
            return;
        }
        // The keyword's own line indent is what the next line inherits --
        // the same column the token itself now sits at.
        EmitIfChanged(edits, text, at, end, "\n" + std::string(LineIndentAt(text, at)));
    }

} // namespace

std::vector<FormatTextEdit> ComputeBreakEdits(std::string_view text, std::string_view languageKey,
                                              const std::vector<FormatCapture>& captures) {
    std::vector<FormatTextEdit> edits;
    for (const FormatCapture& capture : captures) {
        if (capture.startByte >= capture.endByte || capture.endByte > text.size()) {
            continue;
        }
        const BreakRuleValue rule = BreakRuleFor(capture.name, languageKey);
        if (!rule.before && !rule.after) {
            continue; // unconfigured -- no built-in default, nothing forced
        }
        if (rule.before && !BreakUnsafeForLanguage(languageKey, *rule.before)) {
            BreakBefore(edits, text, capture.startByte, *rule.before, captures);
        }
        if (rule.after && !BreakUnsafeForLanguage(languageKey, *rule.after)) {
            BreakAfter(edits, text, capture.endByte, *rule.after);
        }
    }
    return edits;
}

} // namespace ned::editor
