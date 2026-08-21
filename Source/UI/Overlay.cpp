#include "Overlay.h"

#include <algorithm>
#include <utility>

namespace ned::ui {

void OverlayHost::Add(Widget& widget, PlacementFn placement) {
    widget.active = false;
    entries_.push_back(Entry{.widget = &widget, .placement = std::move(placement)});
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

void OverlayHost::Paint(Screen& screen) const {
    for (const Entry& entry : entries_) {
        if (entry.widget->active) {
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
