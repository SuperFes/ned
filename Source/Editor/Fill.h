//
// fill-paragraph (M-q): Emacs-class prose/comment reflow, the one item
// ROADMAP.md's Editor Ergonomics section calls out by name as missing
// despite the stated Emacs-parity vision. Pure functions over Buffer's
// already-public API (Content(), DeleteRange, InsertAt, BeginUndoGroup/
// EndUndoGroup, ...) -- Buffer itself gains no new primitives for this, the
// same "UI-agnostic, composes Buffer's public surface" shape Rectangle.h
// already establishes.
//
// Deliberately independent of Mode.h: FillParagraph takes an optional
// comment-prefix string rather than a Mode, so this file (and its tests)
// have no dependency on the mode/highlighting layer -- Commands.cpp is what
// reads context.mode->lineCommentPrefix and passes it through, the same
// call-site-reads-Mode convention toggle-line-comment already uses.
//
// Width throughout is counted in Unicode codepoints, not display columns
// (no tab-expansion/double-width-glyph awareness) -- fill-paragraph only
// ever operates on prose/comment text, where that distinction essentially
// never matters in practice; a documented v1 scope cut, not an oversight.
//

#ifndef NED_EDITOR_FILL_H
#define NED_EDITOR_FILL_H

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Text/Buffer.h"
#include "Text/ITextStorage.h"

namespace ned::editor {

// Detects a Markdown/Org/reST-style list marker at the very start of `body`
// -- a bullet ("-", "*", "+") or an ordinal ("1.", "12)"), always followed
// by real whitespace, optionally followed by a GFM task checkbox
// ("[ ]"/"[x]"/"[X]") and ITS own trailing whitespace. Returns the marker's
// total width (through its final trailing whitespace, where the real
// content starts), or nullopt if `body` doesn't open with one. Deliberately
// mode-agnostic, unlike Editor/Languages/Markdown.cpp's own list handling:
// this syntax is unambiguous wherever it appears -- a real word never
// starts "- " or "1. " -- so treating it specially is correct in a
// plain-text paragraph, an Org list, or a Doxygen-style bulleted comment
// alike. list-aware-fill-paragraph follow-up originally; soft-wrap-list-
// hang follow-up is its second caller (UI/BufferView/Internal.h's own
// LeadingIndentColumns) -- one detector, so a marker shape either fails to
// recognize can't drift between the hard-wrap (M-q) and soft-wrap (word
// wrap) cases.
[[nodiscard]] std::optional<std::size_t> DetectListMarker(std::string_view body);

// Greedily packs words (assumed to contain no whitespace themselves) into
// lines of at most `width` codepoints, one space between words on the same
// line. Never splits a word -- a single word wider than `width` still gets
// its own line and simply overflows, matching every other editor's
// fill-paragraph rather than hyphenating. Pure, buffer-free.
[[nodiscard]] std::vector<std::string> WrapWords(const std::vector<std::string>& words, std::size_t width);

// The byte range of the paragraph containing `point`'s own line, where a
// paragraph is a maximal run of consecutive non-blank lines ("blank"
// meaning empty or whitespace-only) -- start is the first line's own start,
// end is the last line's content end (excluding its trailing newline, if
// any). If point's own line is blank, scans forward for the next non-blank
// line and returns that line's paragraph instead (matching real Emacs:
// fill-paragraph run from inside a blank gap fills the paragraph ahead of
// point, not behind it). Returns nullopt if no non-blank line exists at or
// after point.
[[nodiscard]] std::optional<std::pair<std::size_t, std::size_t>> FindParagraphRange(const text::ITextStorage& content,
                                                                                    std::size_t               point);

// Re-wraps the paragraph at point to fillColumn codepoints, in place, as one
// undo step. commentPrefix, if non-empty, is a per-line comment leader
// (e.g. "//"): only when *every* line of the paragraph starts with it
// (after leading whitespace) is the prefix stripped before wrapping and
// reattached to every output line -- a mixed paragraph (some lines
// prefixed, some not) is left as plain text instead, since there's no
// sensible single prefix to reattach. list-aware-fill-paragraph follow-up:
// if the paragraph's own FIRST line opens with a Markdown/Org-style list
// marker ("- ", "1. ", "- [ ] ", ...) right after any leading whitespace
// and comment leader, that marker is kept verbatim on the first output line
// and every OTHER output line hangs under it (indent + marker's own width,
// as plain spaces) instead of reusing the first line's raw leading
// whitespace -- without this, the marker itself got folded into the word
// stream and every wrapped continuation line lost the hang entirely. Every
// other paragraph (no marker detected) still just reuses the first line's
// own leading whitespace as every output line's indent, unchanged. No-op if
// FindParagraphRange finds nothing. Moves point to the end of the refilled
// paragraph; mark (if any) is left untouched -- unlike toggle-line-comment,
// fill-paragraph always operates on "the paragraph at point," never a
// region.
void FillParagraph(text::Buffer& buffer, std::size_t fillColumn, std::string_view commentPrefix = {});

} // namespace ned::editor

#endif // NED_EDITOR_FILL_H
