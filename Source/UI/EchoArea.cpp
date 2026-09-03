#include "EchoArea.h"

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
    const Color dimmedForeground   = Color::Interpolate(0.5F, theme_.echoArea.foreground, theme_.echoArea.background);
    // Faded further than plain dim (closer to the background) since ghost
    // text represents a hint, not real candidate-list content -- it should
    // read as clearly less present than DimForEchoArea's own text.
    const Color ghostedForeground = Color::Interpolate(0.7F, theme_.echoArea.foreground, theme_.echoArea.background);

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

        Cell& cell     = c[{.x = x, .y = 0}];
        cell.character = message_.substr(i, next - i);
        theme_.echoArea.ApplyTo(cell);
        if (emphasize) {
            cell.bold = true;
        }
        if (dim) {
            cell.foreground_color = dimmedForeground;
        }
        if (ghost) {
            cell.foreground_color = ghostedForeground;
            cell.italic           = true;
        }
        if (error) {
            cell.foreground_color = theme_.diagnosticError;
        }
        ++x;
        i = next;
    }

    for (; x < c.size().width; ++x) {
        Cell& cell     = c[{.x = x, .y = 0}];
        cell.character = " ";
        theme_.echoArea.ApplyTo(cell);
    }
}

} // namespace ned::ui
