//
// Vim text objects (di( / ciw / ya" / ...) as pure functions over a Buffer, the
// ObjectRange counterpart to VimMotion.h's MotionResult. VimEngine.h calls one of these
// once it's read the "i"/"a" + object-char pair following an operator.
//
// Count support (e.g. "2iw" selecting two words) is wired for the word and sentence
// objects only -- real vim's own most commonly-counted cases, "extend by N more of the
// same unit" for both. Bracket/quote/paragraph/tag objects stay count-1-only: real vim's
// "N levels of nesting outward" semantics for e.g. "2i(" is a materially different
// algorithm, a documented v1 cut. A count typed before the *operator* (not the object
// itself) still repeats the whole operator+object combo either way, unaffected by any of
// this.
//

#ifndef NED_EDITOR_VIM_VIMTEXTOBJECT_H
#define NED_EDITOR_VIM_VIMTEXTOBJECT_H

#include <cstddef>

#include "Text/Buffer.h"
#include "VimTypes.h"

namespace ned::editor::vim {

[[nodiscard]] ObjectRange InnerWord(const text::Buffer& buffer, std::size_t point, bool bigWord, long count = 1);  // iw / iW
[[nodiscard]] ObjectRange AroundWord(const text::Buffer& buffer, std::size_t point, bool bigWord, long count = 1); // aw / aW

// quote is one of '"', '\'', '`'. Scans the current line only (real vim's own scope for
// quote objects).
[[nodiscard]] ObjectRange InnerQuote(const text::Buffer& buffer, std::size_t point, char32_t quote);  // i" / i' / i`
[[nodiscard]] ObjectRange AroundQuote(const text::Buffer& buffer, std::size_t point, char32_t quote); // a" / a' / a`

// open/close is one of ()/[]/{}/<>. Scans the whole buffer (brackets commonly span
// lines), tracking nesting depth the same way VimMotion::MatchPair does.
[[nodiscard]] ObjectRange InnerBracket(const text::Buffer& buffer, std::size_t point, char32_t open, char32_t close);  // i( / i[ / i{ / i<
[[nodiscard]] ObjectRange AroundBracket(const text::Buffer& buffer, std::size_t point, char32_t open, char32_t close); // a( / a[ / a{ / a<

[[nodiscard]] ObjectRange InnerParagraph(const text::Buffer& buffer, std::size_t point);  // ip
[[nodiscard]] ObjectRange AroundParagraph(const text::Buffer& buffer, std::size_t point); // ap

// Sentence boundaries use the same "., !, ?" end-mark convention as
// Buffer::MoveForwardSentence/MoveBackwardSentence (Text/Buffer.cpp) -- kept in sync with
// that logic by hand rather than calling into it directly: those methods are *motions*
// (their own documented real-vim-faithful behavior is that landing exactly on a
// sentence's first character and moving backward again jumps to the *previous* sentence),
// which is the wrong shape for a text object's "the sentence containing point" query
// (point already at a sentence's own first character must still select that sentence).
[[nodiscard]] ObjectRange InnerSentence(const text::Buffer& buffer, std::size_t point, long count = 1);  // is
[[nodiscard]] ObjectRange AroundSentence(const text::Buffer& buffer, std::size_t point, long count = 1); // as

// Tag objects (it/at) -- plain byte/text scanning for the nearest enclosing
// <name ...> ... </name> pair, tracking a stack of open tag names the same way
// FindEnclosingBracket tracks nesting depth (VimTextObject.cpp, anonymous namespace).
// Self-closing tags (<br/>) are skipped entirely, never treated as enclosing anything.
// Not a real HTML/XML parser or tree-sitter-backed -- consistent with every other object
// in this file.
[[nodiscard]] ObjectRange InnerTag(const text::Buffer& buffer, std::size_t point);  // it
[[nodiscard]] ObjectRange AroundTag(const text::Buffer& buffer, std::size_t point); // at

} // namespace ned::editor::vim

#endif // NED_EDITOR_VIM_VIMTEXTOBJECT_H
