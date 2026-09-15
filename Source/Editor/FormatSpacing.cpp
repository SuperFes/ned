#include "FormatSpacing.h"

#include <algorithm>
#include <optional>

#include "FormatEdit.h"
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

    // keyword-delimiter-captures follow-up (fish's own "begin"+"echo" ->
    // "beginecho"): a GENERAL hazard, not a per-language one -- removing
    // the LAST bit of separation between a delimiter token and adjacent
    // content fuses them into one word whenever BOTH edge bytes are word
    // bytes (letter/digit/underscore). Reuses `FormatBracePlacement.h`'s
    // own `IsWordByte` (shared via `FormatEdit.h`, the same fusion check
    // collapse-empty's glue already needed) -- a brace/paren delimiter is
    // never a word byte itself, so this is a no-op for every capture
    // before fish's own, and it needs no language check at all: whether
    // gluing two identifiers together is dangerous is a fact about
    // characters, not about which language they're in. Checked only for
    // `:within=false` (removal) -- `:within=true` can only ever ADD
    // separation, never fuse anything.
    void EmitWithinIfSafe(std::vector<FormatTextEdit>& edits, std::string_view text, const Gap& gap, bool wantSpace,
                          char delimiterEdgeByte, std::optional<char> contentEdgeByte) {
        if (!wantSpace && contentEdgeByte && IsWordByte(delimiterEdgeByte) && IsWordByte(*contentEdgeByte)) {
            return; // would fuse the delimiter with adjacent content into one word
        }
        EmitIfChanged(edits, text, gap, wantSpace);
    }

    // bash-format-revisit follow-up: a SECOND, language-specific hazard,
    // layered on top of the general one above rather than replacing it --
    // `EmitWithinIfSafe`'s word-byte check cannot catch this one, since
    // neither "[" nor "-" (a typical first byte of a test expression) is
    // a word byte. control.parens covers THREE delimiter shapes in bash --
    // "["/"]", "[["/"]]", and a C-style for-loop's own "(("/"))" -- and
    // only the arithmetic "(("/"))" form is safe to compact. "["/"[[" are
    // both ordinary bash WORDS (a command name, a reserved word) needing
    // whitespace separation from their own first/last argument regardless
    // of character class -- confirmed live with a real bash RUN, not just
    // `bash -n`: "[ -n $x]"/"[[-f x]]" both parse (the single-bracket
    // close-side case even passes `bash -n` outright, since `[`'s own
    // argument scanning happens inside the builtin at runtime, not the
    // shell's own parser) but fail when actually run ("[: missing ']'" /
    // "[-f: command not found"). Declined only for the `:within=false`
    // direction, and only for this one gap, not the whole rule: unlike
    // `FormatBracePlacement.h`'s own `PlacementUnsafeForLanguage` (which
    // resets an entire placement value), `:before`/`:after` and a
    // SEPARATE capture's own `:within` stay completely unaffected.
    bool WithinRemovalUnsafe(std::string_view languageKey, std::string_view openText) {
        return languageKey == "bash" && openText != "((";
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
        // keyword-delimiter-captures follow-up: capture.openLength/
        // closeLength generalize past a single-byte "(" "{" -- default 1
        // for every capture before Lua's own, so this is a no-op change
        // for them.
        const bool withinLongEnough =
            rule.within && capture.endByte - capture.startByte >= capture.openLength + capture.closeLength;
        const bool withinRemovalDeclined =
            withinLongEnough && !*rule.within &&
            WithinRemovalUnsafe(languageKey, text.substr(capture.startByte, capture.openLength));
        if (withinLongEnough && !withinRemovalDeclined) {
            const Gap openGap  = HorizontalGapAfter(text, capture.startByte + capture.openLength);
            const Gap closeGap = HorizontalGapBefore(text, capture.endByte - capture.closeLength);
            const char                openDelimiterEdge  = text[capture.startByte + capture.openLength - 1];
            const char                closeDelimiterEdge = text[capture.endByte - capture.closeLength];
            const std::optional<char> openContentEdge =
                openGap.end < text.size() ? std::optional<char>(text[openGap.end]) : std::nullopt;
            EmitWithinIfSafe(edits, text, openGap, *rule.within, openDelimiterEdge, openContentEdge);
            // A genuinely empty pair ("()", nothing between the delimiters)
            // has openGap and closeGap sitting at the exact same position --
            // emitting both would double-insert ("(  )" instead of "( )").
            // Emit the second only when it's a real, distinct gap.
            if (openGap.start != closeGap.start || openGap.end != closeGap.end) {
                const std::optional<char> closeContentEdge =
                    closeGap.start > 0 ? std::optional<char>(text[closeGap.start - 1]) : std::nullopt;
                EmitWithinIfSafe(edits, text, closeGap, *rule.within, closeDelimiterEdge, closeContentEdge);
            }
        }
    }

    std::sort(edits.begin(), edits.end(), [](const FormatTextEdit& a, const FormatTextEdit& b) { return a.start < b.start; });
    return edits;
}

} // namespace ned::editor
