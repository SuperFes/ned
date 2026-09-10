#include "Overlay.h"

#include "ThemePaints.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

namespace ned::ui {

void OverlayHost::Add(Widget& widget, PlacementFn placement, std::string surfaceName) {
    widget.active = false;
    entries_.push_back(
        Entry{.widget = &widget, .placement = std::move(placement), .surfaceName = std::move(surfaceName)});
}

void OverlayHost::Show(Widget& widget) {
    Entry* entry = FindEntry(widget);
    if (entry == nullptr) {
        return;
    }
    entry->widget->active = true;
    if (entry->placement) {
        entry->widget->SetBox_(entry->placement(lastSize_));
    }
    // Raise to topmost by rotating this entry to the back of paint order.
    const auto it = entries_.begin() + (entry - entries_.data());
    std::rotate(it, it + 1, entries_.end());
}

void OverlayHost::Hide(Widget& widget) {
    Entry* entry = FindEntry(widget);
    if (entry == nullptr || !entry->widget->active) {
        return;
    }
    entry->widget->active = false;
    if (entry->widget->Focused() && entry->onFocusReturn) {
        entry->onFocusReturn();
    }
}

void OverlayHost::SetFocusReturn(Widget& widget, std::function<void()> onFocusReturn) {
    if (Entry* entry = FindEntry(widget)) {
        entry->onFocusReturn = std::move(onFocusReturn);
    }
}

bool OverlayHost::IsVisible(const Widget& widget) const {
    const Entry* entry = FindEntry(widget);
    return entry != nullptr && entry->widget->active;
}

void OverlayHost::Reflow(Size terminalSize) {
    lastSize_ = terminalSize;
    for (Entry& entry : entries_) {
        if (entry.widget->active && entry.placement) {
            entry.widget->SetBox_(entry.placement(terminalSize));
        }
    }
}

void OverlayHost::SetTheme(const Theme* theme) {
    theme_ = theme;
}

namespace {

    // Translucency phase 7c. The shadow is the widget's box shifted by
    // (dx, dy) and grown by `radius`, minus the box itself -- so it only ever
    // darkens what surrounds the overlay, never what the overlay is about to
    // paint over.
    //
    // Elevation scales the offset and defaults to 0, which is what keeps this
    // entirely inert until a theme asks for it: at 0 there is no offset, so
    // the shifted box is the box and the region is empty.
    //
    // Coverage falls off with Chebyshev distance beyond the shifted box, so a
    // radius of 0 is a hard-edged offset block and anything larger softens.
    // Blended rather than assigned: a shadow is a wash over whatever is
    // already there, and Screen::Blend's own rules decide what that means for
    // a cell carrying a glyph (tint its foreground) versus an empty one
    // (composite, or dither over a transparent theme).
    void PaintShadow(Screen& screen, const Box& box, const Surface& surface) {
        const Shadow& shadow = surface.shadow;
        if (surface.elevation <= 0 || shadow.colour.alpha == 0) {
            return;
        }

        const int dx     = shadow.dx * surface.elevation;
        const int dy     = shadow.dy * surface.elevation;
        const int radius = std::max(0, shadow.radius);
        if (dx == 0 && dy == 0 && radius == 0) {
            return;
        }

        const Box shifted{.x_min = box.x_min + dx, .x_max = box.x_max + dx, .y_min = box.y_min + dy, .y_max = box.y_max + dy};

        for (int y = shifted.y_min - radius; y <= shifted.y_max + radius; ++y) {
            for (int x = shifted.x_min - radius; x <= shifted.x_max + radius; ++x) {
                if (x < 0 || y < 0 || x >= screen.Width() || y >= screen.Height()) {
                    continue;
                }
                if (box.Contain(x, y)) {
                    continue; // the overlay covers this cell itself
                }

                // Distance outside the shifted box, 0 while within it.
                const int beyondX = std::max({0, shifted.x_min - x, x - shifted.x_max});
                const int beyondY = std::max({0, shifted.y_min - y, y - shifted.y_max});
                const int beyond  = std::max(beyondX, beyondY);
                if (beyond > radius) {
                    continue;
                }

                const double coverage = radius > 0 ? 1.0 - (static_cast<double>(beyond) / (radius + 1)) : 1.0;
                const auto   alpha =
                    static_cast<std::uint8_t>(std::lround(shadow.colour.alpha * std::clamp(coverage, 0.0, 1.0)));
                if (alpha == 0) {
                    continue;
                }

                Cell wash;
                wash.background_color = shadow.colour.WithAlpha(alpha);
                screen.Blend(x, y, wash);
            }
        }
    }

} // namespace

void OverlayHost::Paint(Screen& screen) const {
    for (const Entry& entry : entries_) {
        if (entry.widget->active) {
            if (theme_ != nullptr) {
                PaintShadow(screen, entry.widget->Box_(), SurfaceFor(*theme_, entry.surfaceName));
            }
            entry.widget->Paint(Canvas(screen, entry.widget->Box_()));
        }
    }
}

bool OverlayHost::OnMouseEvent(const Event& event) {
    if (!event.is_mouse()) {
        return false;
    }
    const MouseEvent mouse = event.mouse();
    for (auto it = entries_.rbegin(); it != entries_.rend(); ++it) {
        if (it->widget->active && it->widget->Box_().Contain(mouse.at.x, mouse.at.y)) {
            it->widget->OnEvent(event);
            return true;
        }
    }
    return false;
}

OverlayHost::Entry* OverlayHost::FindEntry(const Widget& widget) {
    for (Entry& entry : entries_) {
        if (entry.widget == &widget) {
            return &entry;
        }
    }
    return nullptr;
}

const OverlayHost::Entry* OverlayHost::FindEntry(const Widget& widget) const {
    for (const Entry& entry : entries_) {
        if (entry.widget == &widget) {
            return &entry;
        }
    }
    return nullptr;
}

} // namespace ned::ui
