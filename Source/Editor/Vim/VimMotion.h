//
// Vim motions as pure functions over a Buffer: (buffer, point, ...) -> MotionResult.
// Never mutate the buffer -- VimEngine.h calls these both to move point directly (plain
// motion) and to compute an operator's target range (operator-pending), the one thing
// that makes Vim's "any operator + any motion" composition need a real function per
// motion rather than Command.h's usual "command directly mutates Buffer" shape.
//
// Word/WORD classification and vertical-motion column math are ASCII-only, matching
// Buffer::MoveForwardWord's own documented word-char scope cut.
//

#ifndef NED_EDITOR_VIM_VIMMOTION_H
#define NED_EDITOR_VIM_VIMMOTION_H

#include <cstddef>

#include "Text/Buffer.h"
#include "VimTypes.h"

namespace ned::editor::vim {

[[nodiscard]] MotionResult CharLeft(const text::Buffer& buffer, std::size_t point, long count);  // h
[[nodiscard]] MotionResult CharRight(const text::Buffer& buffer, std::size_t point, long count); // l

[[nodiscard]] MotionResult LineStartMotion(const text::Buffer& buffer, std::size_t point);           // 0
[[nodiscard]] MotionResult FirstNonBlankMotion(const text::Buffer& buffer, std::size_t point);       // ^
[[nodiscard]] MotionResult LineEndMotion(const text::Buffer& buffer, std::size_t point, long count); // $

// goalColumn is the caller-maintained "sticky" visual column (VimEngine's own field,
// mirroring Buffer::Cursor::goalColumn's concept -- kept out here since these functions
// are pure/non-mutating and Buffer's own goal-column state is private to its Emacs-side
// MoveDownLines/MoveUpLines).
[[nodiscard]] MotionResult LineDown(const text::Buffer& buffer, std::size_t point, long count, std::size_t goalColumn,
                                    std::size_t tabWidth); // j
[[nodiscard]] MotionResult LineUp(const text::Buffer& buffer, std::size_t point, long count, std::size_t goalColumn,
                                  std::size_t tabWidth); // k

// count == 0 means "no count given" (gg -> first line, G -> last line); otherwise a
// 1-based target line, clamped to the buffer's real line range.
[[nodiscard]] MotionResult GotoFirstLine(const text::Buffer& buffer, long count); // gg
[[nodiscard]] MotionResult GotoLastLine(const text::Buffer& buffer, long count);  // G

// bigWord selects WORD semantics (blank-vs-non-blank only, no word/punctuation split).
[[nodiscard]] MotionResult WordForward(const text::Buffer& buffer, std::size_t point, long count, bool bigWord);    // w / W
[[nodiscard]] MotionResult WordBackward(const text::Buffer& buffer, std::size_t point, long count, bool bigWord);   // b / B
[[nodiscard]] MotionResult WordEndForward(const text::Buffer& buffer, std::size_t point, long count, bool bigWord); // e / E

// Backward to the end of the previous word (always strictly before point, never the tail
// of the word point is currently inside -- mirrors WordEndForward's own "step at least one
// grapheme before scanning" guarantee, reversed).
[[nodiscard]] MotionResult WordEndBackward(const text::Buffer& buffer, std::size_t point, long count, bool bigWord); // ge / gE

// till selects t/T (land one grapheme short of target); forward selects f/t vs F/T.
// found is false (no-op) when the count-th occurrence doesn't exist on the current line
// -- f/F/t/T never cross a line boundary.
[[nodiscard]] MotionResult FindChar(const text::Buffer& buffer, std::size_t point, long count, char32_t target,
                                    bool forward, bool till);

[[nodiscard]] MotionResult ParagraphForward(const text::Buffer& buffer, std::size_t point, long count);  // }
[[nodiscard]] MotionResult ParagraphBackward(const text::Buffer& buffer, std::size_t point, long count); // {

// Scans forward from point along the current line for the first bracket character, then
// finds its match across the whole buffer. found is false if point's line has no bracket
// at/after it, or the bracket found is unbalanced.
[[nodiscard]] MotionResult MatchPair(const text::Buffer& buffer, std::size_t point); // %

// topLine/viewportHeight are the host UI's own live viewport facts (Buffer itself has no
// notion of one) -- BufferView supplies these, CommandContext::viewportHeight's own
// "a UI fact a command needs" shape.
[[nodiscard]] MotionResult ScreenTop(const text::Buffer& buffer, std::size_t topLine, std::size_t viewportHeight);    // H
[[nodiscard]] MotionResult ScreenMiddle(const text::Buffer& buffer, std::size_t topLine, std::size_t viewportHeight); // M
[[nodiscard]] MotionResult ScreenBottom(const text::Buffer& buffer, std::size_t topLine, std::size_t viewportHeight); // L

} // namespace ned::editor::vim

#endif // NED_EDITOR_VIM_VIMMOTION_H
