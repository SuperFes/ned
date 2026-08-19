#include "ModeLine.h"

#include <string>

namespace ned::ui {

ModeLine::ModeLine(const ActiveBuffer& activeBuffer, const editor::Mode& mode, const Theme& theme) : activeBuffer_(activeBuffer), mode_(mode), theme_(theme) {
}

void ModeLine::Paint(Canvas c) {
    const text::Buffer& buffer    = activeBuffer_.Get();
    const auto&         content   = buffer.Content();
    const std::size_t   point     = buffer.Point();
    const std::size_t   line      = content.ByteOffsetToLine(point);
    const std::size_t   lineStart = content.LineToByteOffset(line);
    const std::size_t   col       = content.ByteOffsetToCodepointOffset(point) - content.ByteOffsetToCodepointOffset(lineStart);

    // Assumes an ASCII-ish buffer name for correct column alignment (a
    // multi-byte-UTF-8 name would render byte-by-byte here) -- a known,
    // narrow v1 rendering limitation, consistent with BufferView's own
    // codepoint-granular rendering simplification.
    const std::string modifiedMarker = buffer.Modified() ? "*" : " "; // fixed width -- keeps L/C from jittering
    const std::string text           = "  " + modifiedMarker + buffer.Name() + "   L" + std::to_string(line + 1) + ":C" +
                                       std::to_string(col + 1) + "  (" + mode_.name + ")";

    for (int x = 0; x < c.size().width; ++x) {
        Cell& cell     = c[{.x = x, .y = 0}];
        cell.character = (static_cast<std::size_t>(x) < text.size()) ? std::string(1, text[static_cast<std::size_t>(x)]) : " ";

        // A left-to-right gradient across the whole row rather than a flat
        // fill -- the one "gradient" deliverable of Phase 6, chosen because
        // it's always visible without needing any animation/timer machinery.
        const float percent   = (c.size().width > 1) ? static_cast<float>(x) / static_cast<float>(c.size().width - 1) : 0.0F;
        cell.background_color = Color::Interpolate(percent, theme_.modeLineGradientStart, theme_.modeLineGradientEnd);
        cell.foreground_color = theme_.modeLineForeground;
    }
}

} // namespace ned::ui
