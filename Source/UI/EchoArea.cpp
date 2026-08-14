#include "EchoArea.h"

namespace ned::ui {

EchoArea::EchoArea(const std::string& message, const Theme& theme)
    : Widget{ox::FocusPolicy::None, ox::SizePolicy::flex()}, message_(message), theme_(theme) {}

void EchoArea::paint(ox::Canvas c) {
    for (int x = 0; x < c.size.width; ++x) {
        const char32_t symbol = (static_cast<std::size_t>(x) < message_.size())
                                     ? static_cast<char32_t>(static_cast<unsigned char>(message_[static_cast<std::size_t>(x)]))
                                     : U' ';
        c[{.x = x, .y = 0}] = ox::Glyph{.symbol = symbol, .brush = theme_.echoArea};
    }
}

} // namespace ned::ui
