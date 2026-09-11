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

                // Empty rather than the default " " -- see PaintScrim below
                // for the same trap. This comment's own claim ("Blend's
                // rules decide what a shadow means over a glyph: tint it")
                // was false while the default space was left in place: the
                // glyph was replaced, not tinted.
                Cell wash;
                wash.character.clear();
                wash.background_color = shadow.colour.WithAlpha(alpha);
                screen.Blend(x, y, wash);
            }
        }
    }

    // Translucency phase 5. Everything the focused overlay does not cover,
    // washed with the "scrim" surface -- the de-emphasis half of "this is
    // what you are typing into", the other half being the focused mode
    // line's own accent.
    //
    // Blended rather than assigned, so Screen::Blend's rules decide what a
    // scrim means per cell: over a glyph it tints the foreground (dimming
    // the text rather than covering it), over an empty cell it composites,
    // and over a transparent theme it dithers. That is also why it reads
    // correctly over the buffer, the gutters, the mode line and the tab bar
    // without any of them knowing about it.
    //
    // Gated on an overlay actually holding keyboard focus, not merely being
    // visible: a completion popup is up while you type into the buffer
    // behind it, and dimming what you are typing would be exactly backwards.
    void PaintScrim(Screen& screen, const Box& exempt, const Surface& surface) {
        if (!PaintsColour(surface.fill)) {
            return;
        }
        const int width  = screen.Width();
        const int height = screen.Height();
        for (int y = 0; y < height; ++y) {
            const double v = height > 1 ? static_cast<double>(y) / (height - 1) : 0.0;
            for (int x = 0; x < width; ++x) {
                if (exempt.Contain(x, y)) {
                    continue;
                }
                const double u      = width > 1 ? static_cast<double>(x) / (width - 1) : 0.0;
                const Color  colour = PaintColourAt(surface.fill, u, v, x, y);
                if (colour.alpha == 0) {
                    continue;
                }
                // An EMPTY character, not the default " ": Screen::Blend
                // treats a space as a glyph the caller meant to write and
                // overwrites the destination's own. A scrim that erased the
                // text it is meant to de-emphasise would be a curtain.
                //
                // Foreground as well as background, which the shadow above
                // does not do and needs to be said plainly: over an *opaque*
                // theme the buffer's cells already hold the theme
                // background, so a background-only scrim composites that
                // colour onto itself and changes nothing visible. It is the
                // foreground wash (Blend's "a colour with no glyph of its
                // own moves the colour already there") that actually dims
                // the text. Confirmed by a test that failed on exactly this.
                Cell wash;
                wash.character.clear();
                wash.background_color = colour;
                wash.foreground_color = colour;
                screen.Blend(x, y, wash);
            }
        }
    }

} // namespace

void OverlayHost::Paint(Screen& screen) const {
    // Topmost focused overlay wins, matching paint order: entries_ is in
    // back-to-front order, so the last focused one is the one on top.
    const Entry* focusedEntry = nullptr;
    for (const Entry& entry : entries_) {
        if (entry.widget->active && entry.widget->Focused()) {
            focusedEntry = &entry;
        }
    }
    if (theme_ != nullptr && focusedEntry != nullptr) {
        PaintScrim(screen, focusedEntry->widget->Box_(), SurfaceFor(*theme_, "scrim"));
    }

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
