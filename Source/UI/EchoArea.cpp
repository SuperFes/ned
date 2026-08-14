#include "EchoArea.h"

namespace ned::ui {

EchoArea::EchoArea(const std::string& message, const Theme& theme) : message_(message), theme_(theme) {}

void EchoArea::Paint(Canvas c) {
    // Byte-by-byte, not UTF-8-aware -- matches the pre-migration widget's
    // own documented ASCII-ish assumption (see ModeLine's identical
    // limitation, below), not a new limitation introduced by this port.
    for (int x = 0; x < c.size().width; ++x) {
        ftxui::Cell& cell = c[{.x = x, .y = 0}];
        cell.character = (static_cast<std::size_t>(x) < message_.size())
                              ? std::string(1, message_[static_cast<std::size_t>(x)])
                              : " ";
        theme_.echoArea.ApplyTo(cell);
    }
}

} // namespace ned::ui
