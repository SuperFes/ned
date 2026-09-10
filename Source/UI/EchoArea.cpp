#include "EchoArea.h"

#include "Paint.h"
#include "ThemePaints.h"

#include "Text/Utf8.h"

namespace ned::ui {

namespace {

    // fuzzy-candidate-list-styling follow-up: C0 control bytes 1-4, never
    // legitimately present in a status message (paths/command names/regex
    // patterns/prose) -- see EchoArea.h's own doc comment on
    // EmphasizeForEchoArea/DimForEchoArea for the full rationale. Kept
    // file-private: BufferView.cpp (the only real caller today) reaches
    // these only through the two encode functions, never the raw bytes.
    constexpr char kEmphasisStart = '\x01';
    constexpr char kEmphasisEnd   = '\x02';
    constexpr char kDimStart      = '\x03';
    constexpr char kDimEnd        = '\x04';
    constexpr char kGhostStart    = '\x05';
    constexpr char kGhostEnd      = '\x06';
    // partial-match-highlighting follow-up: next free pair in the same
    // never-legitimately-present-in-a-status-message C0 range above.
    constexpr char kErrorStart = '\x07';
    constexpr char kErrorEnd   = '\x08';

} // namespace

std::string EmphasizeForEchoArea(std::string_view text) {
    return kEmphasisStart + std::string(text) + kEmphasisEnd;
}

std::string DimForEchoArea(std::string_view text) {
    return kDimStart + std::string(text) + kDimEnd;
}

std::string GhostForEchoArea(std::string_view text) {
    return kGhostStart + std::string(text) + kGhostEnd;
}

std::string ErrorForEchoArea(std::string_view text) {
    return kErrorStart + std::string(text) + kErrorEnd;
}

EchoArea::EchoArea(const std::string& message, const Theme& theme) : message_(message), theme_(theme) {
}

void EchoArea::Paint(Canvas c) {
    // Codepoint-by-codepoint, not grapheme-cluster-aware -- matches
    // BufferView's own one-codepoint-per-cell content rendering, not the
    // byte-by-byte walk this used to do (that corrupted any multi-byte UTF-8
    // character into as many blank cells as it had bytes; found live via
    // lsp-signature-help, see ROADMAP.md). The kEmphasis*/kDim*/kGhost*
    // sentinels (see EchoArea.h) are always exactly one byte, so a
    // one-byte span is checked against them before falling through to the
    // general case -- consumed as zero-width markup rather than indexed by
    // column the way plain characters are, which is why the loop below
    // walks message_ by codepoint span rather than directly indexing it by
    // x the way the pre-sentinel version did.
    // Translucency phase 5: the row is a themed Surface, ModeLine's own
    // precedent and paint order -- clear, fill, glyphs, then any text fade.
    // The derived default is the flat echoArea Brush this used to apply
    // per cell, so an unthemed editor is unchanged.
    //
    // Cleared to ChromeBackdrop rather than the theme's own background: the
    // echo area is a bar, so a translucent fill should fade *into* the
    // buffer's colour rather than fall down the dither path (see
    // ThemePaints.h's ClearCanvas note, and ProjectSidebar for the opposite
    // case).
    const Surface surface = SurfaceFor(theme_, "echo");
    ClearCanvas(c, ChromeBackdrop(theme_));
    Fill(c, surface.fill);

    // Dim and ghost interpolate toward whatever the fill actually put down
    // at that cell, not toward the Brush's flat background -- identical for
    // the derived default, and the only reading that stays right when a
    // theme gives the row a gradient. Read per cell below; this is the
    // fallback for a cell whose background is the terminal's own.
    const Color fallbackBackground = ChromeBackdrop(theme_);

    int         x         = 0;
    bool        emphasize = false;
    bool        dim       = false;
    bool        ghost     = false;
    bool        error     = false;
    std::size_t i         = 0;
    while (i < message_.size()) {
        const std::size_t next = text::NextCodepointBoundary(message_, i);
        if (next - i == 1) {
            const char ch = message_[i];
            if (ch == kEmphasisStart) {
                emphasize = true;
                i         = next;
                continue;
            }
            if (ch == kEmphasisEnd) {
                emphasize = false;
                i         = next;
                continue;
            }
            if (ch == kDimStart) {
                dim = true;
                i   = next;
                continue;
            }
            if (ch == kDimEnd) {
                dim = false;
                i   = next;
                continue;
            }
            if (ch == kGhostStart) {
                ghost = true;
                i     = next;
                continue;
            }
            if (ch == kGhostEnd) {
                ghost = false;
                i     = next;
                continue;
            }
            if (ch == kErrorStart) {
                error = true;
                i     = next;
                continue;
            }
            if (ch == kErrorEnd) {
                error = false;
                i     = next;
                continue;
            }
        }
        if (x >= c.size().width) {
            break; // rest of the message doesn't fit -- truncated, same as the pre-sentinel version
        }

        const Point at{.x = x, .y = 0};
        const Color painted = c[at].background_color.Composable() ? c[at].background_color : fallbackBackground;
        const Color base    = TextColourAt(surface, c, at, theme_.echoArea.foreground);

        // Written into the cell rather than blended, because these carry
        // traits (Surface has none of its own -- see ROADMAP) and the
        // background is already down. Leaving background_color alone is what
        // keeps the fill underneath intact.
        Cell& cell            = c[at];
        cell.character        = message_.substr(i, next - i);
        cell.foreground_color = base;
        cell.bold             = theme_.echoArea.bold || emphasize;
        cell.italic           = theme_.echoArea.italic || ghost;
        cell.underlined       = theme_.echoArea.underlined;
        cell.strikethrough    = theme_.echoArea.strikethrough;
        if (dim) {
            cell.foreground_color = Color::Interpolate(0.5F, base, painted);
        }
        if (ghost) {
            // Faded further than plain dim (closer to the background) since
            // ghost text represents a hint, not real candidate-list content
            // -- it should read as clearly less present than
            // DimForEchoArea's own text.
            cell.foreground_color = Color::Interpolate(0.7F, base, painted);
        }
        if (error) {
            cell.foreground_color = theme_.diagnosticError;
        }
        ++x;
        i = next;
    }

    for (; x < c.size().width; ++x) {
        const Point at{.x = x, .y = 0};
        Cell&       cell      = c[at];
        cell.character        = " ";
        cell.foreground_color = TextColourAt(surface, c, at, theme_.echoArea.foreground);
        cell.bold             = theme_.echoArea.bold;
        cell.italic           = theme_.echoArea.italic;
        cell.underlined       = theme_.echoArea.underlined;
        cell.strikethrough    = theme_.echoArea.strikethrough;
    }

    ApplyTextFade(c, surface);
}

} // namespace ned::ui
