//
// Org-like structured editing (v1 scope, see ROADMAP.md's "Org-like
// structured editing" entry). Two layers in this one file:
//
// 1. A pure structural model -- ParseOutline/ParseCheckboxes take plain
//    buffer text (string_view in, structs out), the same "UI-agnostic,
//    independently unit tested" shape ProjectTree.h/ProjectSearch.h already
//    establish, and the same "whole buffer text, not Buffer&" shape Mode.h's
//    HighlightFunction uses, for the same reason: nothing here needs live
//    point/mark/undo, only the text.
// 2. A thin Buffer-mutating layer on top (HeadlineAtPoint, SetHeadline*,
//    Cycle*AtPoint, ToggleCheckboxAtPoint) -- these edit an already-parsed
//    Headline/Checkbox's own byte range in place via Buffer's existing
//    public DeleteRange/InsertAt, the same "Buffer gains no new text-
//    manipulation primitives" precedent Rectangle.h/ProjectReplace.h
//    already set (Buffer itself still has zero Org-specific knowledge).
//
// What's still not here: folding a subtree in BufferView, tables, links,
// and real tree-sitter-org highlighting -- separate follow-up slices. Where
// per-buffer fold state (which subtrees are collapsed) should live is a
// real open design question, left open rather than guessed at: Buffer
// already carries some non-text state (Point_/Mark_/narrowedRange_), which
// argues for keeping fold state there too, but a naive ProjectSidebar-style
// set<Buffer*> cache owned by BufferView would repeat a dangling-buffer-
// pointer bug class this codebase has already hit and fixed twice (see
// RegisterTable's point registers, which resolve a buffer by name instead
// of holding a raw Buffer* for exactly that reason).
//

#ifndef NED_EDITOR_ORG_H
#define NED_EDITOR_ORG_H

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Text/Buffer.h"

namespace ned::editor::org {

// {"TODO", "DONE"} -- Org's own minimal default keyword set. A real install
// typically configures more (e.g. "TODO" / "IN-PROGRESS" / "DONE").
[[nodiscard]] std::vector<std::string> DefaultTodoKeywords();

// Process-wide, mutex-guarded static state (mirrors TabWidth.h's exact
// pattern), defaulting to DefaultTodoKeywords(). What org-cycle-todo/
// HeadlineAtPoint's own default argument reads -- kept separate from
// DefaultTodoKeywords() so the pure parsing functions below stay
// decoupled from any global, mutable state in their own default argument
// (tests call ParseOutline directly with an explicit list, or get
// DefaultTodoKeywords() -- never whatever this happens to be set to).
// No ned/set-org-todo-keywords Janet binding exists yet -- Value.h has no
// std::vector<std::string> marshalling to build one on top of yet, a real
// (if mechanical) piece of follow-up work, not attempted here; matches
// this codebase's own repeated "hardcoded C++ for now" scope cut (e.g.
// the page-scroll fraction, initial Theme selection).
void                                    SetTodoKeywords(std::vector<std::string> keywords);
[[nodiscard]] const std::vector<std::string>& TodoKeywords();

// One headline line (a line starting with one or more '*' at column 0 --
// leading whitespace before the stars means it's NOT a headline, which is
// what actually distinguishes a headline from an indented list item; this
// is real Org's own rule, not an invented simplification).
struct Headline {
    int                      level;       // number of leading stars, >= 1
    std::string              todoKeyword; // empty if the headline has none
    std::optional<char>      priority;    // 'A'-'Z' from a [#X] cookie; nullopt if none
    std::string              title;       // TODO keyword, priority cookie, and tags all stripped, trimmed
    std::vector<std::string> tags;        // from a trailing :tag1:tag2: block, in file order
    std::size_t              lineNumber;  // 0-indexed, matching Buffer::ByteOffsetForLineAndColumn's own convention
    std::size_t              lineStartByte;
    std::size_t              lineEndByte; // exclusive, before the line's own trailing '\n' (if any)
};

// Scans every line of bufferText and returns the headlines found, in file
// order. todoKeywords is checked against the token immediately following
// the stars (and a mandatory following space or end-of-line, so "TODOING"
// is never misread as the keyword "TODO" plus title "ING") -- defaults to
// DefaultTodoKeywords(). A non-headline line (including a blank line, or an
// indented "* not a headline") contributes nothing; this never throws, an
// empty/non-Org buffer just yields an empty vector.
[[nodiscard]] std::vector<Headline> ParseOutline(std::string_view                bufferText,
                                                  const std::vector<std::string>& todoKeywords = DefaultTodoKeywords());

// Cycles a headline's TODO state forward through todoKeywords, then back to
// "no keyword" -- e.g. with the default {"TODO", "DONE"}: "" -> "TODO" ->
// "DONE" -> "". current not found in todoKeywords at all (a headline with
// no keyword, or todoKeywords having been reconfigured since the headline
// was last set) is treated the same as "" -- lands on todoKeywords[0]
// rather than throwing, since silently doing nothing on a stale/unknown
// keyword would be a worse surprise than just starting the cycle over.
// Returns "" for an empty todoKeywords list (nothing to cycle to).
[[nodiscard]] std::string NextTodoKeyword(const std::string& current, const std::vector<std::string>& todoKeywords);

// Cycles a headline's priority cookie: nullopt -> 'A' -> 'B' -> 'C' ->
// nullopt. A deliberate v1 simplification against real Emacs Org, which
// splits this into separate raise/lower/remove-priority commands (and
// supports a configurable A-Z range, not just A-C) -- matching this
// codebase's own established pattern of picking the simpler single-cycle
// shape where the full Emacs behavior isn't worth the extra command
// surface yet (e.g. narrowing's whole-line alignment vs. Emacs' exact
// mid-line narrowing).
[[nodiscard]] std::optional<char> NextPriority(std::optional<char> current);

// A checkbox list item (a line like "  - [ ] Buy milk" or "- [X] Done").
// Deliberately '-'/'+' bullets only, not Org's numbered-list checkboxes
// (`1. [ ] ...`) -- matches the ROADMAP entry's own stated v1 syntax.
// Nesting is inferred purely from indent (a greater indent than the
// preceding checkbox nests under it) and never stored as a real tree here
// -- the same "flat list, depth is a field, structure derived at use time"
// shape ProjectTreeEntry already establishes for the (unrelated) project
// file tree.
struct Checkbox {
    std::size_t indent;     // leading whitespace count
    char        state;      // ' ' (unchecked), 'X' or 'x' (checked), or '-' (partial -- see ReflectParentCheckboxStates)
    std::string text;       // the item's own text, after "[state] "
    std::size_t lineNumber; // 0-indexed
    std::size_t stateByte;  // byte offset of the state character itself -- always exactly 1 byte, so toggling it in
                             // place never shifts any other checkbox's own stateByte
};

[[nodiscard]] std::vector<Checkbox> ParseCheckboxes(std::string_view bufferText);

// ' '/'-' (unchecked/partial) -> 'X'; 'X'/'x' (checked) -> ' ' -- matches
// real Org's own C-c C-c toggle behavior (partial counts as "not done yet").
[[nodiscard]] char ToggleCheckboxState(char current);

// Recomputes every checkbox's own state from its *direct* children only
// (a checkbox with no children is returned unchanged) -- all children
// checked -> 'X', all unchecked -> ' ', anything else (including a '-'
// among the children) -> '-'. Processes bottom-up internally, so a deep
// change already correctly propagates through an intermediate parent
// before that parent's own state is folded into its grandparent. Doesn't
// mutate items in place -- returns the recomputed list so callers can diff
// against the original to find which lines actually need a buffer edit.
[[nodiscard]] std::vector<Checkbox> ReflectParentCheckboxStates(std::vector<Checkbox> items);

// The headline whose own line (lineStartByte..lineEndByte inclusive)
// contains buffer.Point() -- nullopt if point is anywhere else (inside a
// subtree's body, on a blank line, ...). Real Org's own org-todo/
// org-priority both require point on the heading line itself, not just
// somewhere in its subtree.
[[nodiscard]] std::optional<Headline> HeadlineAtPoint(const text::Buffer&              buffer,
                                                       const std::vector<std::string>& todoKeywords = TodoKeywords());

// Rewrites headline's own TODO keyword/priority cookie in place ("" /
// nullopt removes it entirely, including its own separating space).
// headline's byte offsets must describe buffer's *current* content --
// always obtained via a fresh HeadlineAtPoint/ParseOutline call against
// this same buffer, never cached across an unrelated edit.
void SetHeadlineTodoKeyword(text::Buffer& buffer, const Headline& headline, const std::string& newKeyword);
void SetHeadlinePriority(text::Buffer& buffer, const Headline& headline, std::optional<char> newPriority);

// The two Buffer-mutating commands proper: find the headline at point,
// cycle its TODO keyword/priority, write it back. Return false (buffer
// untouched) if point isn't on a headline line -- callers report e.g. "Not
// on a headline." the same way rectangle commands report "No rectangle
// region selected." for their own missing-precondition case.
bool CycleTodoKeywordAtPoint(text::Buffer& buffer, const std::vector<std::string>& todoKeywords = TodoKeywords());
bool CyclePriorityAtPoint(text::Buffer& buffer, const std::vector<std::string>& todoKeywords = TodoKeywords());

// Toggles the checkbox at point's own line, then rewrites every ancestor
// checkbox whose own state actually changes as a result (via
// ReflectParentCheckboxStates) -- all in a handful of single-byte in-place
// edits, never shifting any other line's byte offsets. Returns false
// (buffer untouched) if point isn't on a checkbox line.
bool ToggleCheckboxAtPoint(text::Buffer& buffer);

} // namespace ned::editor::org

#endif // NED_EDITOR_ORG_H
