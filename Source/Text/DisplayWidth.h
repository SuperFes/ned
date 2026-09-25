//
// How many terminal columns text occupies. The renderer is Notcurses, so
// its measurement (ncstrwidth) is the authority: anything that lines text up
// on screen -- the buffer view, goal columns, fill, table alignment, widget
// labels -- asks here rather than counting codepoints.
//
// The unit is the grapheme cluster (UAX #29, the same segmentation
// Grapheme.h moves the cursor by): a base character with its combining
// marks, or an emoji ZWJ sequence, is one glyph in one cell (two for a wide
// glyph). A cluster that is invisible or unsafe to send to a terminal --
// a C0/C1 control, a zero-width format character (bidi overrides, ZWSP, a
// BOM), a combining mark with no base -- is drawn as a hex placeholder
// ("◁200B▷") instead, and measures as that placeholder's width. A byte that
// isn't part of well-formed UTF-8 is a glyph of its own, drawn as its value
// ("◁\xE9▷"), so a file in another encoding shows what it actually holds.
//
// Measurement needs a UTF-8 LC_CTYPE. Under any other locale Notcurses
// cannot measure (or print) non-ASCII text, and every printable cluster
// counts as one column -- the pre-width-aware behaviour.
//

#ifndef NED_TEXT_DISPLAYWIDTH_H
#define NED_TEXT_DISPLAYWIDTH_H

#include <cstddef>
#include <string>
#include <string_view>

namespace ned::text {

class ITextStorage;

struct Glyph {
    std::size_t byteLength  = 0;     // the whole cluster
    char32_t    codepoint   = 0;     // its first codepoint
    int         columns     = 1;     // cells it occupies; a tab reports 1 -- see GlyphColumns
    bool        placeholder = false; // drawn as PlaceholderText(glyph), not as itself
    bool        rawByte     = false; // a malformed byte; codepoint is the byte's value
};

// The grapheme cluster starting at offset, never extending past end. A tab
// or control character is always a cluster of its own.
[[nodiscard]] Glyph GlyphAt(const ITextStorage& content, std::size_t offset, std::size_t end);
[[nodiscard]] Glyph GlyphAt(std::string_view text, std::size_t offset);

// Columns glyph occupies when it starts at `column`: a tab runs to the next
// multiple of tabWidth, everything else is glyph.columns.
[[nodiscard]] inline int GlyphColumns(const Glyph& glyph, int column, int tabWidth) {
    if (glyph.codepoint == U'\t' && tabWidth > 0) {
        return tabWidth - (column % tabWidth);
    }
    return glyph.columns;
}

// Hex digits a placeholder spells its codepoint with: 2, 4 or 6.
[[nodiscard]] int PlaceholderDigits(char32_t codepoint);
// The placeholder's text, "◁XX▷", and its width (digits plus the brackets).
[[nodiscard]] std::string PlaceholderText(char32_t codepoint);
// A placeholder glyph's text: its codepoint's, or "◁\xE9▷" for a raw byte.
[[nodiscard]] std::string PlaceholderText(const Glyph& glyph);
[[nodiscard]] inline int  PlaceholderColumns(char32_t codepoint) {
    return PlaceholderDigits(codepoint) + 2;
}

// What a glyph looks like in its cell: the cluster's codepoints re-encoded
// or the placeholder.
[[nodiscard]] std::string GlyphText(const ITextStorage& content, std::size_t offset, const Glyph& glyph);
[[nodiscard]] std::string GlyphText(std::string_view text, std::size_t offset, const Glyph& glyph);

// Total columns text occupies starting at startColumn (which only matters
// for tabs).
[[nodiscard]] int StringColumns(std::string_view text, int startColumn = 0, int tabWidth = 1);

// The longest prefix of text, in bytes, that fits in `columns` -- never
// splitting a cluster.
[[nodiscard]] std::size_t PrefixBytesForColumns(std::string_view text, int columns);

} // namespace ned::text

#endif // NED_TEXT_DISPLAYWIDTH_H
