//
// Two independent, togglable rendering settings for BufferView::Paint:
// trailing-whitespace highlighting (spaces/tabs after the last non-
// whitespace character on a line) and leading-whitespace indentation
// guides (a vertical glyph at each indent-width column within a line's
// own leading whitespace run). Mutex-guarded static state, mirroring
// TabWidth.h/.cpp's exact pattern.
//

#ifndef NED_EDITOR_WHITESPACESETTINGS_H
#define NED_EDITOR_WHITESPACESETTINGS_H

namespace ned::editor {

// Default false -- VSCode/Sublime/JetBrains default this on, but Emacs has
// no built-in equivalent turned on by default either (closest analog,
// `show-trailing-whitespace`, is opt-in); ned follows that precedent, same
// reasoning already applied to auto-pair (a togglable feature, not a
// forced-on default).
void               SetTrailingWhitespaceHighlightEnabled(bool enabled);
[[nodiscard]] bool TrailingWhitespaceHighlightEnabled();

// Default false, same reasoning as above.
void               SetIndentGuidesEnabled(bool enabled);
[[nodiscard]] bool IndentGuidesEnabled();

// Depth-colorized-indent-guides follow-up: cycles each guide column's color
// through Theme::indentGuideDepthPalette by its own nesting level (1st
// indent level, 2nd, ...) instead of the flat indentGuideForeground --
// purely a per-column arithmetic recoloring (displayColumn / tabWidth), no
// fold/tree-sitter data needed, so it applies uniformly to every mode,
// including ones with no fold query at all. Default true: unlike
// IndentGuidesEnabled above (which decides whether guides render at all),
// this only changes their color once guides are already on, so there's no
// equivalent "editors don't do this by default" precedent to follow.
void               SetIndentGuideDepthColorsEnabled(bool enabled);
[[nodiscard]] bool IndentGuideDepthColorsEnabled();

} // namespace ned::editor

#endif // NED_EDITOR_WHITESPACESETTINGS_H
