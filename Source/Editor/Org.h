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
// 3. Subtree fold/unfold (real Org's 3-state TAB cycle: Collapsed ->
//    ChildrenVisible -> Expanded), built on Buffer::FoldMarker -- a
//    generic, Org-agnostic {byte offset -> marker} map Buffer relocates
//    across edits exactly like Point_/Mark_/NarrowedRange_ (see Buffer.h's
//    own FoldMarker doc comment). This is what finally answers the open
//    design question the previous two slices left open ("where should
//    per-buffer fold state live") -- Buffer, generalizing its own existing
//    non-text-state precedent, rather than a BufferView-owned
//    set<Buffer*>-keyed cache, which would have repeated a dangling-
//    buffer-pointer bug class this codebase has already hit and fixed
//    twice (see RegisterTable's point registers, which resolve a buffer by
//    name instead of holding a raw Buffer* for exactly that reason).
//    BuildHeadlineTree/FoldedLineRanges are what turn ParseOutline's flat,
//    level-tagged list plus Buffer's generic markers into real Org subtree
//    visibility; CycleFoldAtPoint is the actual TAB command body.
//
// 4. Table parsing + column alignment, built on Source/Editor/Table.h's
//    shared, format-agnostic {SplitRow/ComputeColumnWidths/PadCell/
//    FindTableBlockLines} toolkit -- Source/Editor/Markdown.h's own GFM
//    table support is the toolkit's other consumer, since the two formats'
//    column-width/alignment engine is identical even though their exact
//    separator-row syntax isn't (Org: a plain `-+-` hrule, no per-column
//    alignment marker; GFM: a mandatory `:---`/`:---:`/`---:` delimiter
//    row). Only column alignment is in scope -- formula support is
//    explicitly deferred, see ROADMAP.md.
//
// 5. Links: real Org's own [[target][description]] bracket syntax --
//    ParseLinks/LinkAtPoint are the pure-parse/AtPoint pair every other
//    construct here already follows; FindHeadlineByTitle backs the internal-
//    link form ([[*Some Headline]]). Bare-URL/file-path detection for every
//    OTHER mode -- the generic half of this feature -- deliberately does NOT
//    live here: see Source/Editor/Link.h's own header comment for why that's
//    a separate, Org-agnostic file, and BufferView::OpenLinkAtPoint for how
//    the two are stitched together (Org's own bracket links first in an
//    org-mode buffer, falling back to Link.h's generic detection everywhere
//    else, including for an Org link target that isn't a headline). The
//    actual visual "hide the brackets, show only the description" rendering
//    lives entirely in Source/UI/BufferView.cpp -- this file only parses.
//
// What's still not here: real tree-sitter-org highlighting -- a separate
// follow-up slice, not started.
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
void                                          SetTodoKeywords(std::vector<std::string> keywords);
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
    // Where the tags block (plus its own separating whitespace before it,
    // and any trailing whitespace after it) begins -- equal to lineEndByte
    // when there's no tags block at all, so SetHeadlineTags's own
    // delete/insert logic works identically either way (an empty
    // [tagsStartByte, lineEndByte) delete range is simply a no-op).
    std::size_t tagsStartByte;
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
[[nodiscard]] std::optional<Headline> HeadlineAtPoint(const text::Buffer&             buffer,
                                                      const std::vector<std::string>& todoKeywords = TodoKeywords());

// Rewrites headline's own TODO keyword/priority cookie in place ("" /
// nullopt removes it entirely, including its own separating space).
// headline's byte offsets must describe buffer's *current* content --
// always obtained via a fresh HeadlineAtPoint/ParseOutline call against
// this same buffer, never cached across an unrelated edit.
void SetHeadlineTodoKeyword(text::Buffer& buffer, const Headline& headline, const std::string& newKeyword);
void SetHeadlinePriority(text::Buffer& buffer, const Headline& headline, std::optional<char> newPriority);

// Rewrites headline's own trailing tags block in place -- an empty newTags
// removes the block entirely (including its own separating space and any
// trailing whitespace after it). Unlike SetHeadlineTodoKeyword/
// SetHeadlinePriority, tags are free-form user text, not a small fixed set
// to cycle through, so this is driven by a real prompt (BufferView's own
// InputMode::SetHeadlineTags, org-set-tags command) rather than an
// AtPoint-style cycling wrapper -- there's no "next" tag set to compute.
// Same "headline's byte offsets must describe buffer's current content"
// precondition as SetHeadlineTodoKeyword/SetHeadlinePriority.
void SetHeadlineTags(text::Buffer& buffer, const Headline& headline, std::vector<std::string> newTags);

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

// A real parent/child tree built from ParseOutline's flat, level-tagged
// list -- a headline is the direct child of the nearest preceding headline
// with a strictly smaller level; multiple headlines at the shallowest
// level present become multiple root nodes. `headline` points into the
// same `headlines` vector BuildHeadlineTree was called with -- the
// returned tree must not outlive it.
struct HeadlineNode {
    const Headline*           headline = nullptr;
    std::vector<HeadlineNode> children;
};

[[nodiscard]] std::vector<HeadlineNode> BuildHeadlineTree(const std::vector<Headline>& headlines);

// The line index one past headlines[index]'s subtree: the next headline in
// headlines (file order) with level <= headlines[index].level, or
// totalLines if none exists.
[[nodiscard]] std::size_t SubtreeEndLine(const std::vector<Headline>& headlines, std::size_t index,
                                         std::size_t totalLines);

// [start, end) buffer-line ranges currently hidden by buffer's own
// FoldMarkers() -- Buffer itself has zero Org-specific knowledge (see this
// file's own top comment); this is where those generic markers become
// real Org subtree visibility. Built by walking
// BuildHeadlineTree(ParseOutline(...)) once, node-local, no propagated
// "forced" flags needed: a node marked Collapsed hides [its own line + 1,
// subtree end) and is NOT recursed into at all -- descendants' own
// markers are simply not consulted while an ancestor hides them, left
// exactly as they are for whenever the ancestor reopens. A node marked
// ChildrenVisible hides only its own leading body text (up to its first
// child's line, or subtree end if it has none) and recurses into each
// child per that child's OWN marker. An unmarked node recurses normally.
// A marker offset that no longer lands exactly on a real headline's own
// line start after an edit is silently never consulted (not an error) --
// same stale-state tolerance ProjectSidebar's own accumulated view state
// already established. Empty if buffer.FoldMarkers() is empty, without
// even re-parsing the outline -- the common-case fast path for every
// buffer that's never had a fold touched.
[[nodiscard]] std::vector<std::pair<std::size_t, std::size_t>>
FoldedLineRanges(const text::Buffer& buffer, const std::vector<std::string>& todoKeywords = TodoKeywords());

// Advances the fold state of the headline at point one step through real
// Org's own TAB cycle: Expanded -> Collapsed -> ChildrenVisible ->
// Expanded.
//   Expanded -> Collapsed: sets the headline's own marker to Collapsed.
//     Descendants' own markers are left untouched (irrelevant while
//     hidden, preserved for whenever this reopens).
//   Collapsed -> ChildrenVisible: sets the headline's own marker to
//     ChildrenVisible, AND force-sets every DIRECT child's marker to
//     Collapsed (overwriting whatever was there) -- deterministic, matches
//     the expected "list of child headlines, bodies closed" look every
//     time, rather than trying to preserve nuanced prior interior state
//     across a full collapse/reopen.
//   ChildrenVisible -> Expanded: clears the headline's own marker AND
//     recursively clears every descendant's -- real Org's third state is a
//     full subtree expand, no exceptions.
// No point-safety clamp is needed: HeadlineAtPoint already requires point
// to sit on the headline's own line, and every transition above only ever
// hides lines strictly after it, so point can never end up hidden by its
// own headline's fold. Returns false (buffer/fold state untouched) if
// point isn't on a headline line.
bool CycleFoldAtPoint(text::Buffer& buffer, const std::vector<std::string>& todoKeywords = TodoKeywords());

// A contiguous run of lines each starting with '|' (Org's own table-line
// rule; see Source/Editor/Table.h's FindTableBlockLines, which this is
// built on). rows/isSeparatorRow are parallel, one entry per line in
// [startLine, endLine); a separator row's own `rows` entry is always empty
// -- it carries no real content, only a hrule shape, and Org's separator
// rows carry no per-column alignment marker either (every column aligns
// Left in this v1 -- see this file's own top comment). A data row that
// happens to consist entirely of '-'/'+'/'|'/whitespace characters (e.g. a
// literal "-" placeholder in every cell) is indistinguishable from a real
// separator row by this same rule -- an inherent ambiguity in Org's own
// table syntax, not a gap specific to this parser.
struct OrgTable {
    std::vector<std::vector<std::string>> rows;
    std::vector<bool>                     isSeparatorRow;
    std::size_t                           startLine;
    std::size_t                           endLine; // exclusive
};

[[nodiscard]] std::optional<OrgTable> FindOrgTableAtPoint(const text::Buffer& buffer);

// Realigns every column in the table at point to its content's own width,
// then moves point to the next cell in row-major order (wrapping from the
// table's last cell back to its first) -- matches real Org's own TAB
// behavior inside a table. Auto-inserting a new row when tabbing past the
// last cell of the last row (real Org's own behavior too) is explicitly
// deferred, not attempted here -- a real, separate piece of buffer
// mutation (inserting a whole new line, not just realigning existing
// ones); see ROADMAP.md. Returns false (buffer untouched) if point isn't
// inside a table.
bool AlignOrgTableAtPoint(text::Buffer& buffer);

// A real Org link: "[[target]]" (no description -- display the target
// itself) or "[[target][description]]". Org links never span lines (same
// single-line assumption ParseOutline/ParseCheckboxes/the tags-block regex
// already make). startByte/endByte cover the WHOLE bracket span, including
// both "[[" and "]]" -- this is what Source/UI/BufferView.cpp's descriptive-
// link rendering collapses down to just DisplayText() on screen.
struct Link {
    std::string target;
    std::string description; // empty means "no description given"
    std::size_t startByte;
    std::size_t endByte; // exclusive
};

// What's actually shown on screen when this link is collapsed: description
// if one was given, target otherwise -- shared by BufferView's rendering and
// anything else that needs "the one string this link reads as."
[[nodiscard]] inline const std::string& LinkDisplayText(const Link& link) {
    return link.description.empty() ? link.target : link.description;
}

// Scans every line of bufferText for "[[target]]"/"[[target][description]]"
// spans, in file order -- a line may contain more than one. Pure, like
// ParseOutline/ParseCheckboxes; never throws.
[[nodiscard]] std::vector<Link> ParseLinks(std::string_view bufferText);

// The link whose own [startByte, endByte) contains buffer.Point() -- nullopt
// if point isn't inside any link. Read-only (doesn't mutate buffer), the
// same "*AtPoint" naming HeadlineAtPoint already establishes despite also
// being read-only.
[[nodiscard]] std::optional<Link> LinkAtPoint(const text::Buffer& buffer);

// Backs an internal link's own target form, "*Some Headline Title" (real
// Org's own in-file link syntax, the '*' stripped by the caller before
// calling this) -- an exact match (after trimming) against ParseOutline's
// own Headline.title, case-sensitive. Returns the matching headline's own
// lineStartByte, or nullopt if nothing matches. Real Org also supports
// "#custom-id" against a property drawer's :CUSTOM_ID: -- explicitly out of
// scope, property drawers don't exist in this codebase yet (see ROADMAP.md's
// "v2+ maybe" list), not silently dropped.
[[nodiscard]] std::optional<std::size_t> FindHeadlineByTitle(std::string_view bufferText, std::string_view title);

} // namespace ned::editor::org

#endif // NED_EDITOR_ORG_H
