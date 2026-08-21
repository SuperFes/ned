#include "EchoArea.h"

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

EchoArea::EchoArea(const std::string& message, const Theme& theme) : message_(message), theme_(theme) {
}

void EchoArea::Paint(Canvas c) {
    // Byte-by-byte, not UTF-8-aware -- matches the pre-migration widget's
    // own documented ASCII-ish assumption (see ModeLine's identical
    // limitation, below), not a new limitation introduced by this port. The
    // kEmphasis*/kDim* sentinels (see EchoArea.h) are consumed as zero-width
    // markup rather than indexed by column the way plain characters are --
    // this is why the loop below walks message_ sequentially instead of
    // directly indexing it by x the way the pre-sentinel version did.
    const Color dimmedForeground   = Color::Interpolate(0.5F, theme_.echoArea.foreground, theme_.echoArea.background);
    // Faded further than plain dim (closer to the background) since ghost
    // text represents a hint, not real candidate-list content -- it should
    // read as clearly less present than DimForEchoArea's own text.
    const Color ghostedForeground = Color::Interpolate(0.7F, theme_.echoArea.foreground, theme_.echoArea.background);

    int  x         = 0;
    bool emphasize = false;
    bool dim       = false;
    bool ghost     = false;
    for (const char ch : message_) {
        if (ch == kEmphasisStart) {
            emphasize = true;
            continue;
        }
        if (ch == kEmphasisEnd) {
            emphasize = false;
            continue;
        }
        if (ch == kDimStart) {
            dim = true;
            continue;
        }
        if (ch == kDimEnd) {
            dim = false;
            continue;
        }
        if (ch == kGhostStart) {
            ghost = true;
            continue;
        }
        if (ch == kGhostEnd) {
            ghost = false;
            continue;
        }
        if (x >= c.size().width) {
            break; // rest of the message doesn't fit -- truncated, same as the pre-sentinel version
        }

        Cell& cell     = c[{.x = x, .y = 0}];
        cell.character = std::string(1, ch);
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
        ++x;
    }

    for (; x < c.size().width; ++x) {
        Cell& cell     = c[{.x = x, .y = 0}];
        cell.character = " ";
        theme_.echoArea.ApplyTo(cell);
    }
}

} // namespace ned::ui
