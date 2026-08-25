//
// Vim text objects (di( / ciw / ya" / ...) as pure functions over a Buffer, the
// ObjectRange counterpart to VimMotion.h's MotionResult. VimEngine.h calls one of these
// once it's read the "i"/"a" + object-char pair following an operator.
//
// No count support on the text object itself (e.g. "2iw" selecting two words) -- a
// documented v1 simplification; a count typed before the operator still repeats the
// whole operator+object combo the ordinary way. it/at (tag objects) and is/as (sentence
// objects) are also deliberate v1 cuts, not wired into VimEngine yet.
//

#ifndef NED_EDITOR_VIM_VIMTEXTOBJECT_H
#define NED_EDITOR_VIM_VIMTEXTOBJECT_H

#include <cstddef>

#include "Text/Buffer.h"
#include "VimTypes.h"

namespace ned::editor::vim {

[[nodiscard]] ObjectRange InnerWord(const text::Buffer& buffer, std::size_t point, bool bigWord);  // iw / iW
[[nodiscard]] ObjectRange AroundWord(const text::Buffer& buffer, std::size_t point, bool bigWord); // aw / aW

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

} // namespace ned::editor::vim

#endif // NED_EDITOR_VIM_VIMTEXTOBJECT_H
