#include "MemoryImageView.h"

#include <algorithm>

#include "Border.h"
#include "Editor/MemoryImage.h"
#include "KeyTranslation.h"
#include "Paint.h"
#include "ThemePaints.h"

namespace ned::ui {

namespace {

    Color ToUiColor(const editor::MemoryImageColor& color) {
        return Color::RGB(color.r, color.g, color.b);
    }

} // namespace

MemoryImageView::MemoryImageView(const Theme& theme) : theme_(theme) {
}

void MemoryImageView::SetModel(MemoryImageModel model) {
    model_ = std::move(model);
}

void MemoryImageView::SetOnCancel(std::function<void()> onCancel) {
    onCancel_ = std::move(onCancel);
}

void MemoryImageView::Paint(Canvas c) {
    const int width  = c.size().width;
    const int height = c.size().height;
    if (width <= 0 || height <= 0) {
        return;
    }

    const Brush interiorBrush{.background = theme_.background, .foreground = theme_.defaultForeground};

    // Translucency phase 7: the body is the shared "popup" Surface, so this
    // panel cannot drift from ListPopup/TreeView. Derived default is the flat
    // theme background this used to apply per cell.
    //
    // The background is cleared first unless the fill reads it (a blur
    // samples what the overlay covers) -- ListPopup's own rule, and what
    // keeps a transparent theme's do-nothing default from leaving stale
    // cells behind.
    const Surface surface = SurfaceFor(theme_, "popup");
    const bool    fillReadsDestination =
        surface.fill.kind == PaintKind::Blur || surface.fill.kind == PaintKind::Stack;
    for (int y = 1; y < height - 1; ++y) {
        for (int x = 1; x < width - 1; ++x) {
            Cell& cell            = c[{.x = x, .y = y}];
            cell.character        = " ";
            cell.foreground_color = interiorBrush.foreground;
            cell.bold             = false;
            cell.italic           = false;
            cell.underlined       = false;
            cell.strikethrough    = false;
            cell.inverted         = false;
            if (!fillReadsDestination) {
                cell.background_color = interiorBrush.background;
            }
        }
    }

    const Point origin   = c.Origin();
    Canvas      interior = c.ForBox(
        Box{.x_min = origin.x + 1, .x_max = origin.x + width - 2, .y_min = origin.y + 1, .y_max = origin.y + height - 2});
    Fill(interior, surface.fill);

    DrawBorder(c, theme_.border);
    RecolourBorder(c, surface.border);
    DrawBorderTitle(c, model_.title, theme_.borderAccent);

    const int interiorWidth  = width - 2;
    const int interiorHeight = height - 2;
    if (interiorWidth <= 0 || interiorHeight <= 0 || model_.bytes.empty()) {
        return;
    }

    const editor::MemoryImageLayout layout =
        editor::ComputeMemoryImageLayout(model_.bytes.size(), static_cast<std::size_t>(interiorWidth));
    if (layout.pixelColumns == 0) {
        return;
    }

    // Two pixel rows per cell row (the half-block doubling trick -- see
    // MemoryImage.h's header comment).
    const std::size_t cellRows    = (layout.pixelRows + 1) / 2;
    const int         rowsToPaint = std::min(static_cast<int>(cellRows), interiorHeight);
    const int         xOffset     = 1 + (interiorWidth - static_cast<int>(layout.pixelColumns)) / 2;
    const int         yOffset     = 1 + (interiorHeight - rowsToPaint) / 2;

    for (int ry = 0; ry < rowsToPaint; ++ry) {
        for (std::size_t rx = 0; rx < layout.pixelColumns; ++rx) {
            const std::size_t topIndex = (static_cast<std::size_t>(ry) * 2) * layout.pixelColumns + rx;
            if (topIndex >= model_.bytes.size()) {
                continue; // leave the already-filled background showing
            }
            const std::size_t bottomIndex = topIndex + layout.pixelColumns;

            Cell& cell            = c[{.x = xOffset + static_cast<int>(rx), .y = yOffset + ry}];
            cell.character        = "▀"; // UPPER HALF BLOCK
            cell.foreground_color = ToUiColor(editor::ByteToGrayscale(model_.bytes[topIndex]));
            cell.background_color = bottomIndex < model_.bytes.size()
                                        ? ToUiColor(editor::ByteToGrayscale(model_.bytes[bottomIndex]))
                                        : theme_.background;
        }
    }
}

bool MemoryImageView::OnEvent(const Event& event) {
    if (event.is_mouse()) {
        return true; // consumed -- no interaction beyond dismiss-by-key
    }
    if (!Focused()) {
        return false;
    }
    const auto chord = TranslateKey(event);
    if (!chord) {
        return true; // focused: swallow undecodable input rather than leaking it
    }
    if (onCancel_) {
        onCancel_();
    }
    return true;
}

} // namespace ned::ui
