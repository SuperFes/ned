#include "FormatBracePlacement.h"

#include <algorithm>

#include "FormatRules.h"
#include "Indent.h"
#include "IndentStyle.h"

namespace ned::editor {

namespace {

    // wrap-kind follow-up: IsFormatWhitespace/LineIndentOf moved to
    // FormatEdit.h once a second consumer (FormatWrap.h) needed them.

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
    // fish-format follow-up: the SAME "SameLine glues onto a preceding
    // statement's own terminator" hazard as bash's own "do"/"then" --
    // begin_statement is a bare, standalone statement (no header/
    // condition of its own), so confirmed live with a real fish RUN:
    // "echo hi begin ... end" reads "begin" as an ARGUMENT to "echo"
    // rather than starting a new block ("'end' outside of a block").
    // Both begin_statement's own forms ("begin"/"end" and "{"/"}") share
    // this one capture name, so the decline is the same capture-name-wide
    // shape bash's own guard already uses.
    // coverage-audit follow-up: fish's brace.function (function_definition's
    // own "function"/"end" pair) has the IDENTICAL hazard, for the same
    // reason (a bare, standalone statement) -- confirmed live. bash's own
    // brace.function is UNAFFECTED (real braces, no terminator needed at
    // all), so the two languages' own capture-name sets genuinely differ
    // here, not merely for lack of checking.
    // ruby-rollout follow-up: the openText parameter is new -- bash/fish's
    // own guards above predate it and don't need it (their own hazard is
    // real for every instance sharing the capture name, confirmed live,
    // so a capture-name-wide decline is the correct, not merely
    // convenient, answer for them). Ruby's own "brace.control" is
    // genuinely mixed: "begin" is a bare, standalone statement with the
    // SAME "glues onto whatever precedes it" hazard bash/fish's own
    // do/then/begin_statement have (confirmed live: "foo begin...end"
    // gets swallowed as an argument to "foo" rather than starting a fresh
    // block), while if/unless/while/until's own "then"/"do" are always
    // nested inside their OWN statement's header (the gap SameLine would
    // touch sits between "if x"/"while x" and "then"/"do", never a
    // separate preceding statement) -- confirmed safe by construction, not
    // just untested. Discriminated by the open text itself rather than a
    // capture-name-wide decline, since the signal is available and the
    // safe/unsafe split is real, not speculative.
    bool PlacementUnsafeForLanguage(BracePlacement placement, std::string_view languageKey,
                                    std::string_view captureName, std::string_view openText) {
        if (languageKey == "go") {
            return placement != BracePlacement::SameLine;
        }
        if (languageKey == "bash" && captureName == "brace.control") {
            return placement == BracePlacement::SameLine;
        }
        if (languageKey == "fish" && (captureName == "brace.control" || captureName == "brace.function")) {
            return placement == BracePlacement::SameLine;
        }
        if (languageKey == "ruby" && captureName == "brace.control" && openText == "begin") {
            return placement == BracePlacement::SameLine;
        }
        return false;
    }

    // coverage-audit follow-up: a real corruption hazard found live, not
    // by inspection -- collapse-simple's own interior computation
    // (capture.startByte + capture.openLength through capture.endByte -
    // capture.closeLength) assumes the OPEN token sits directly beside
    // the real body, true for every paired capture in this codebase
    // EXCEPT fish's own brace.function: function_definition's mandatory
    // "name:" field (and optional "option:" fields) sit BETWEEN "function"
    // and the real body. Confirmed live: force-expanding
    // "function greet; echo hello; end" produced "function\n    greet;
    // echo hello;\nend" -- "function" alone on its own line with the name
    // pushed onto the body's own line, a hard `fish -n` syntax error
    // ("Expected a string, but found end of the statement"). The join
    // direction is equally wrong in kind (it would fold the name into the
    // reconstructed "body" text), just not always visibly different from
    // the source. collapse-empty needs no equivalent guard -- its own
    // isEmpty check can never be true here (the name is never whitespace),
    // so it's already, if incidentally, inert rather than merely declined.
    bool CollapseSimpleUnsafeForLanguage(std::string_view languageKey, std::string_view captureName) {
        return languageKey == "fish" && captureName == "brace.function";
    }

    // ruby-rollout follow-up: a real corruption hazard found live, not by
    // inspection, and a genuinely NEW shape -- every other guard in this
    // file keys on (language, captureName) because within a given
    // language, every INSTANCE of a capture name shares the same open-side
    // shape. Ruby's own "brace.function" doesn't: method/singleton_method
    // anchor ".open" on their own NAME field (a real, unavoidable choice --
    // see ruby/format.janet's own header comment for why a real delimiter
    // token can't be used there), while block/do_block anchor on a real
    // "{"/"do" delimiter, both sharing the SAME capture name. Gluing an
    // arbitrary identifier directly against "end" is NOT the word-fusion
    // problem IsWordByte already guards below (a separating space IS
    // inserted) -- confirmed with a real `ruby -c` that "def foo end"
    // still fails ("expected a delimiter to close the parameters") purely
    // from the grammar's own params-continuation ambiguity at that lexical
    // position, regardless of the space -- same for singleton_class's own
    // "class << self end" ("unexpected 'end'; expected a newline or a ';'
    // after the singleton class"). So this keys on the open TOKEN'S OWN
    // TEXT instead (the same finer-grained precedent `FormatSpacing.h`'s
    // `WithinRemovalUnsafe` already set for bash's own `[`-vs-`((`
    // distinction under one shared capture name): unsafe for any
    // word-shaped open text that ISN'T one of Ruby's own fixed keyword
    // opens (do/then/begin -- confirmed with a real `ruby -c` that "do
    // end"/"then end"/"begin end" all pass clean). A punctuation open
    // ("{") is never ambiguous with a following identifier, so it's
    // excluded up front rather than needing its own keyword-list entry.
    //
    // A real `ruby -c` also turned up a genuine imprecision in this
    // decline, found only once the interpreter was actually available:
    // "class Foo end"/"module M end" pass clean too (class/module's own
    // NAME field is a grammar-guaranteed CONSTANT, never ambiguous the way
    // a bare method name is) -- so this declines a real, if minor, safe
    // case for them. Left as-is rather than narrowed: singleton_class
    // shares "brace.class" with plain class, and its own VALUE field is an
    // arbitrary expression (`class << SomeConstant end` is ALSO confirmed
    // unsafe, and "SomeConstant" is textually indistinguishable from a
    // real class name) -- there's no text-only signal here that
    // reliably tells "class's own NAME" apart from "singleton_class's own
    // VALUE," and the feature this would unlock (collapse-empty on a
    // handful of empty class/module bodies) isn't worth risking that
    // distinction being wrong on a construct that IS a real corruption.
    bool CollapseEmptyUnsafeForLanguage(std::string_view languageKey, std::string_view openText) {
        if (languageKey != "ruby" || openText.empty() || !IsWordByte(openText.front())) {
            return false;
        }
        return openText != "do" && openText != "then" && openText != "begin";
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

    // Whether `at` falls inside a "comment" capture -- a brace pulled up onto
    // a header line ending in a line comment would be commented out.
    bool InsideComment(const std::vector<FormatCapture>& captures, std::size_t at) {
        return std::ranges::any_of(captures, [at](const FormatCapture& capture) {
            return capture.name == "comment" && capture.startByte <= at && at < capture.endByte;
        });
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
        // keyword-delimiter-captures follow-up: the open/close TOKEN text
        // itself, not just its length -- "{"/"}"  for every capture before
        // Lua's, "do"/"end" or "then"/"end" for one of Lua's own paired
        // captures. Computed up front (moved ahead of the placement check
        // by the ruby rollout) since both PlacementUnsafeForLanguage and
        // CollapseEmptyUnsafeForLanguage now need the open text too, not
        // just collapse-empty/collapse-simple below.
        const std::string_view openText  = text.substr(capture.startByte, capture.openLength);
        const std::string_view closeText = text.substr(capture.endByte - capture.closeLength, capture.closeLength);

        BreakRuleValue rule = BreakRuleFor(capture.name, languageKey);
        if (rule.placement && PlacementUnsafeForLanguage(*rule.placement, languageKey, capture.name, openText)) {
            rule.placement.reset(); // see PlacementUnsafeForLanguage's own comment
        }
        if (rule.collapseSimple && CollapseSimpleUnsafeForLanguage(languageKey, capture.name)) {
            rule.collapseSimple.reset(); // see CollapseSimpleUnsafeForLanguage's own comment
        }
        if (rule.collapseEmpty && CollapseEmptyUnsafeForLanguage(languageKey, openText)) {
            rule.collapseEmpty.reset(); // see CollapseEmptyUnsafeForLanguage's own comment
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

        const bool joinOntoComment =
            rule.placement == BracePlacement::SameLine && InsideComment(captures, headerEnd - 1);

        if (rule.placement && !joinOntoComment) {
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
