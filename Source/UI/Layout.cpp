#include "Layout.h"

#include <algorithm>

namespace ned::ui {

void Container::LayoutChildren() {
    const Box& box = Box_();
    const int  mainAxisLength =
        (axis_ == Axis::Horizontal) ? std::max(0, box.x_max - box.x_min + 1) : std::max(0, box.y_max - box.y_min + 1);

    int fixedTotal    = 0;
    int flexWeightSum = 0;
    for (const Child& child : children_) {
        if (!child.widget->active) {
            continue;
        }
        switch (child.size.kind) {
            case SizeSpec::Kind::Fixed:
                fixedTotal += child.size.fixedValue;
                break;
            case SizeSpec::Kind::DynamicFixed:
                fixedTotal += child.size.dynamicSize ? child.size.dynamicSize() : 0;
                break;
            case SizeSpec::Kind::Flex:
                flexWeightSum += std::max(0, child.size.flexWeight);
                break;
        }
    }
    const int flexTotal = std::max(0, mainAxisLength - fixedTotal);

    int position        = (axis_ == Axis::Horizontal) ? box.x_min : box.y_min;
    int flexRemaining   = flexTotal;
    int weightRemaining = flexWeightSum;

    for (const Child& child : children_) {
        if (!child.widget->active) {
            continue;
        }

        int length = 0;
        switch (child.size.kind) {
            case SizeSpec::Kind::Fixed:
                length = child.size.fixedValue;
                break;
            case SizeSpec::Kind::DynamicFixed:
                length = child.size.dynamicSize ? child.size.dynamicSize() : 0;
                break;
            case SizeSpec::Kind::Flex: {
                const int weight = std::max(0, child.size.flexWeight);
                // Largest-remainder-ish split: each Flex child gets its
                // proportional share of whatever's left, and the running
                // remainder/weight totals are decremented together so
                // rounding error accumulates into the *last* Flex child
                // rather than being silently dropped or double-counted --
                // the same "divide what's left among what's left" approach
                // avoids needing floating point at all.
                length = (weightRemaining > 0) ? (flexRemaining * weight) / weightRemaining : 0;
                flexRemaining -= length;
                weightRemaining -= weight;
                break;
            }
        }
        length = std::max(0, length);

        Box childBox;
        if (axis_ == Axis::Horizontal) {
            childBox = Box{.x_min = position, .x_max = position + length - 1, .y_min = box.y_min, .y_max = box.y_max};
        }
        else {
            childBox = Box{.x_min = box.x_min, .x_max = box.x_max, .y_min = position, .y_max = position + length - 1};
        }
        child.widget->SetBox_(childBox);
        position += length;
    }
}

void Container::Paint(Canvas c) {
    LayoutChildren();
    for (const Child& child : children_) {
        if (child.widget->active) {
            child.widget->Paint(c.ForBox(child.widget->Box_()));
        }
    }
}

bool Container::OnEvent(const Event& event) {
    bool handled = false;
    for (const Child& child : children_) {
        if (child.widget->active && child.widget->OnEvent(event)) {
            handled = true;
        }
    }
    return handled;
}

} // namespace ned::ui
