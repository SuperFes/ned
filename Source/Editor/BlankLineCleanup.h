//
// smart-blank-line-on-newline follow-up. Whether "newline" (Commands.cpp)
// clears a whitespace-only line's own leading run before splitting it, when
// point is currently on one -- i.e. pressing Enter a second time on a line
// that only ever got auto-indented and never actually typed into removes
// that dangling whitespace instead of leaving it behind. Process-wide,
// mutex-guarded static state, mirroring TrimOnSave.h's exact
// default-on/bool shape. Configured from Janet via
// ned/set-clean-blank-line-on-newline.
//
// Deliberately mode-agnostic and unconditional here -- this alone is what
// gives a tree-sitter-backed mode (Python, say) "clear the abandoned line's
// whitespace, but the new line still indents to the same depth" behavior,
// since clearing a blank line's whitespace doesn't change what a language's
// own block/scope structure resolves to. A markdown/org-style "second Enter
// also breaks out of list continuation, landing at column 0" behavior is a
// further, separate addition inside MarkdownMode()/OrgMode()'s own
// indentColumn closures (Mode.cpp) -- see their comments -- not something
// this generic setting controls on its own.
//

#ifndef NED_EDITOR_BLANKLINECLEANUP_H
#define NED_EDITOR_BLANKLINECLEANUP_H

namespace ned::editor {

void               SetCleanBlankLineOnNewline(bool enabled);
[[nodiscard]] bool CleanBlankLineOnNewline();

} // namespace ned::editor

#endif // NED_EDITOR_BLANKLINECLEANUP_H
