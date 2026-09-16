//
// configurable-formatter follow-up: the tabs-vs-spaces content-detection
// scan the design section's own "Mode line" bullet always assumed would
// exist ("Content-detection (tabs vs. spaces majority scan) applies only to
// FundamentalMode buffers with no configured style at all") but which was
// never actually built alongside Phase 1's per-language IndentStyle
// defaults (Editor/IndentDefaults.h/IndentStyle.h) -- those cover a
// mode-matched buffer; this covers the one case they can't, a buffer with
// no language-specific convention to fall back on at all (a FundamentalMode
// file like /etc/fstab). A second, unplanned use fell out for free once
// this existed: the SAME detector run against a mode-MATCHED buffer's own
// content is what lets Source/UI/ModeLine.cpp flag "this file doesn't
// match its configured style" -- a passive, display-only signal, never
// something that changes what format-buffer/the Indent pass actually does
// (touching that would be the same "surprise the user by overriding their
// file" mistake the design doc's own C2 section already argues against for
// automatic content detection).
//
// Pure and buffer-free (no text::Buffer/Parser/Screen dependency), same
// split every other checker-shaped file in this codebase takes
// (Editor/LocalScopes.h, Editor/FormatCase.h, ...).
//

#ifndef NED_EDITOR_INDENTDETECT_H
#define NED_EDITOR_INDENTDETECT_H

#include <string_view>

namespace ned::editor {

enum class DetectedIndentKind {
    Unknown, // no indented line at all (an empty file, or one with no leading whitespace anywhere)
    Tabs,
    Spaces,
    Mixed, // both tab-led and space-led indented lines appear often enough that neither is clearly "the" convention
};

struct DetectedIndent {
    DetectedIndentKind kind = DetectedIndentKind::Unknown;
    // Meaningful only for Kind::Spaces -- the smallest nonzero leading-
    // space run seen across every space-indented line, the same "first
    // indent level IS the width" heuristic real editors (VS Code's own
    // detectIndentation, among others) use. Defaults to 4 (this codebase's
    // own IndentStyle default) when there is nothing to measure.
    int spacesWidth = 4;
};

// Scans every line of text for its own leading-whitespace run (a blank line,
// or one with no leading whitespace at all, contributes nothing -- a
// top-level line carries no indentation information). A line whose leading
// run mixes tabs and spaces (e.g. a tab then spaces) is counted toward
// whichever character it STARTS with, matching how a real terminal/editor
// actually renders that line's own effective indent depth; only the
// aggregate tabs-led-vs-spaces-led split across the whole file decides
// Kind::Mixed, not any one line's own mixed run.
//
// Mixed is reported when both counts are nonzero AND neither is at least
// four times the other -- a single stray tab in an otherwise all-spaces
// file (or vice versa) stays whichever convention clearly dominates, but a
// file genuinely split between the two conventions (e.g. hand-edited by
// two different tools) is reported as Mixed rather than silently picking
// whichever happened to have one more line.
[[nodiscard]] DetectedIndent DetectIndentStyle(std::string_view text);

} // namespace ned::editor

#endif // NED_EDITOR_INDENTDETECT_H
