#include "ModeLine.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "Editor/BackgroundActivity.h"

namespace ned::ui {

namespace {

    // background-activity-spinner follow-up. Braille spinner frames --
    // matches the minimap's existing braille glyph vocabulary, no patched
    // font or double-width rendering risk (the same reasoning behind
    // ProjectSidebar's own glyph choices). Each frame is one multi-byte
    // UTF-8 glyph occupying exactly one cell, which is why the paint loop
    // below works in per-column cell strings rather than raw bytes.
    constexpr std::array<std::string_view, 10> kSpinnerFrames = {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"};

    std::string_view CurrentSpinnerFrame() {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch());
        return kSpinnerFrames[static_cast<std::size_t>((elapsed / editor::kBackgroundActivitySpinnerInterval) % kSpinnerFrames.size())];
    }

} // namespace

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
    // large-file-async-load follow-up: while the buffer is still filling in
    // from a background AsyncFileLoader, show that instead of the mode name
    // -- there's nothing meaningful to report for L/C or a mode against
    // content that isn't fully there yet, and this is the one existing,
    // already-per-frame-refreshed place a "this is still loading" signal
    // can surface without any new plumbing (buffer.IsLoading() is a plain
    // Buffer query, same as Modified()/Point() just above).
    // large-file-async-load polish: a live percentage when the loader
    // published one (Buffer::CurrentLoadProgress; totalBytes 0 means the
    // size query failed -- fall back to the old plain indicator rather than
    // dividing by it). bytesRead can momentarily exceed totalBytes if the
    // file grew after the size query, hence the clamp.
    std::string loadingText = "   Loading...";
    if (buffer.IsLoading()) {
        if (const text::LoadProgress* progress = buffer.CurrentLoadProgress();
            progress != nullptr && progress->totalBytes > 0) {
            const std::uintmax_t read    = progress->bytesRead.load(std::memory_order_relaxed);
            const std::uintmax_t percent = std::min<std::uintmax_t>(100, read * 100 / progress->totalBytes);
            loadingText += " " + std::to_string(percent) + "%";
        }
    }

    const std::string text = buffer.IsLoading() ? "  " + buffer.Name() + loadingText
                                                : "  " + modifiedMarker + buffer.Name() + "   L" + std::to_string(line + 1) +
                                                      ":C" + std::to_string(col + 1) + "  (" + mode_.name + ")";

    // background-activity-spinner follow-up: one column-per-entry cell list
    // instead of the raw byte string above, so the spinner's multi-byte
    // braille glyph occupies exactly one cell (the byte-per-column loop this
    // replaces would have split it across three). ASCII text still lands one
    // byte per cell, unchanged; an activity detail with multi-byte UTF-8 in
    // it renders byte-by-byte, the same known v1 limitation the buffer-name
    // comment above already documents.
    std::vector<std::string> columns;
    columns.reserve(text.size() + 32);
    for (const char ch : text) {
        columns.emplace_back(1, ch);
    }
    const std::vector<editor::BackgroundActivity> activities = editor::ActiveBackgroundActivities();
    if (!activities.empty()) {
        const std::string_view frame = CurrentSpinnerFrame();
        for (const editor::BackgroundActivity& activity : activities) {
            columns.emplace_back(" ");
            columns.emplace_back(" ");
            for (const char ch : activity.name) {
                columns.emplace_back(1, ch);
            }
            columns.emplace_back(" ");
            columns.emplace_back(frame);
            if (!activity.detail.empty()) {
                columns.emplace_back(" ");
                for (const char ch : activity.detail) {
                    columns.emplace_back(1, ch);
                }
            }
        }
    }

    // Chrome-redesign follow-up: the focused pane's gradient pulls toward
    // the theme accent so which split has the keyboard is visible at a
    // glance -- see SetFocusProvider.
    const bool  focused       = focusProvider_ && focusProvider_();
    const Color gradientStart = focused ? theme_.modeLineFocusedGradientStart : theme_.modeLineGradientStart;
    const Color gradientEnd   = focused ? theme_.modeLineFocusedGradientEnd : theme_.modeLineGradientEnd;

    for (int x = 0; x < c.size().width; ++x) {
        Cell& cell     = c[{.x = x, .y = 0}];
        cell.character = (static_cast<std::size_t>(x) < columns.size()) ? columns[static_cast<std::size_t>(x)] : " ";

        // A left-to-right gradient across the whole row rather than a flat
        // fill -- the one "gradient" deliverable of Phase 6, chosen because
        // it's always visible without needing any animation/timer machinery.
        const float percent   = (c.size().width > 1) ? static_cast<float>(x) / static_cast<float>(c.size().width - 1) : 0.0F;
        cell.background_color = Color::Interpolate(percent, gradientStart, gradientEnd);
        cell.foreground_color = theme_.modeLineForeground;
    }
}

void ModeLine::SetFocusProvider(std::function<bool()> provider) {
    focusProvider_ = std::move(provider);
}

} // namespace ned::ui
