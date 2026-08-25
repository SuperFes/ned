#include "WhichKeyPopup.h"

#include <utility>

#include "Border.h"

namespace ned::ui {

WhichKeyPopup::WhichKeyPopup(const Theme& theme) : theme_(theme) {
}

void WhichKeyPopup::SetHint(WhichKeyHint hint) {
    hint_ = std::move(hint);
}

int WhichKeyPopup::ContentRowCount() const {
    return static_cast<int>(hint_.bindings.size()) + 2; // + top/bottom border rows
}

void WhichKeyPopup::Paint(Canvas c) {
    const int width  = c.size().width;
    const int height = c.size().height;
    if (width <= 0 || height <= 0) {
        return;
    }

    DrawBorder(c, theme_.border);
    DrawBorderTitle(c, hint_.prefixLabel, theme_.borderAccent);

    const Brush chordBrush{.background = theme_.background, .foreground = theme_.borderAccent.foreground, .bold = true};
    const Brush labelBrush{.background = theme_.background, .foreground = theme_.defaultForeground};

    int row = 1;
    for (const auto& [chord, label] : hint_.bindings) {
        if (row >= height - 1) {
            break; // more bindings than fit -- truncated, same convention as EchoArea's own message overflow
        }

        int x = 2;
        for (const char ch : chord) {
            if (x >= width - 1) {
                break;
            }
            Cell& cell     = c[{.x = x, .y = row}];
            cell.character = std::string(1, ch);
            chordBrush.ApplyTo(cell);
            ++x;
        }
        x += 2; // gap between the chord and its command name
        for (const char ch : label) {
            if (x >= width - 1) {
                break;
            }
            Cell& cell     = c[{.x = x, .y = row}];
            cell.character = std::string(1, ch);
            labelBrush.ApplyTo(cell);
            ++x;
        }
        ++row;
    }
}

} // namespace ned::ui
