//
// Org-like structured editing (v1 scope, see ROADMAP.md's "Org-like
// structured editing" entry): the headline/outline model, the first slice
// landed. Deliberately narrower than "Org mode" -- headline structure
// (stars, an optional TODO keyword, an optional [#priority] cookie, a
// trailing :tag: block) is the one piece every other v1 feature (subtree
// fold/unfold, an agenda-shaped view, tag inheritance) ultimately keys off,
// so it comes first. Checkboxes, tables, and links are their own follow-up
// slices, not started here.
//
// Pure functions over plain buffer text (string_view in, structs out) --
// the same "UI-agnostic, independently unit tested" shape ProjectTree.h/
// ProjectSearch.h already establish, and the same "whole buffer text, not
// Buffer&" shape Mode.h's HighlightFunction uses, chosen for the same
// reason: nothing here needs live point/mark/undo, only the text. Buffer
// itself gains no new members for this -- same "Buffer stays unaware"
// precedent Rectangle.h/ProjectReplace.h already set.
//
// What's deliberately NOT here yet: any Buffer-mutating operation (actually
// cycling a headline's TODO keyword in place, folding a subtree in
// BufferView) -- this file is the structural *model* only. Where per-buffer
// fold state should live is a real open design question (Buffer already
// carries some non-text state -- Point_/Mark_/narrowedRange_ -- but fold
// state is more of a view concern, closer to ProjectSidebar's own
// expandedDirs_; a raw Buffer* the way ProjectSidebar keys off a
// filesystem::path isn't safe here, given this codebase's own prior
// dangling-buffer-pointer bugs -- see RegisterTable's point registers,
// which resolve a buffer by name instead) -- left open for the next slice
// rather than guessed at here.
//

#ifndef NED_EDITOR_ORG_H
#define NED_EDITOR_ORG_H

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ned::editor::org {

// {"TODO", "DONE"} -- Org's own minimal default keyword set. A real install
// typically configures more (e.g. "TODO" / "IN-PROGRESS" / "DONE"); nothing
// wires a configurable set in yet (no ned/set-org-todo-keywords binding
// exists), matching how TabWidth/FormatOnSave each landed their own
// process-wide setting only once something needed to actually configure it.
[[nodiscard]] std::vector<std::string> DefaultTodoKeywords();

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

} // namespace ned::editor::org

#endif // NED_EDITOR_ORG_H
