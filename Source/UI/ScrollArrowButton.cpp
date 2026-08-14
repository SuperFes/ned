#include "ScrollArrowButton.h"

namespace ned::ui {

namespace {

    // Fast enough to feel like held-key repeat, slow enough not to blow past
    // the target line while the user reacts and releases. A single fixed
    // interval throughout (no separate, longer "initial delay" before the
    // first repeat) -- a deliberately simple choice over a two-phase or
    // accelerating scheme, matching the size of what was actually asked for.
    constexpr std::chrono::milliseconds kRepeatInterval{120};

} // namespace

ScrollArrowButton::ScrollArrowButton(char32_t symbol, const ox::Brush& brush, const ox::Brush& disabledBrush) : Widget{ox::FocusPolicy::None, ox::SizePolicy::flex()},
                                                                                                                symbol_(symbol),
                                                                                                                brush_(brush),
                                                                                                                disabledBrush_(disabledBrush),
                                                                                                                repeatTimer_(*this, kRepeatInterval) {
}

void ScrollArrowButton::SetOnClick(std::function<void()> onClick) {
    onClick_ = std::move(onClick);
}

void ScrollArrowButton::SetEnabled(bool enabled) {
    enabled_ = enabled;
}

bool ScrollArrowButton::IsRepeating() const {
    return repeatTimer_.is_running();
}

void ScrollArrowButton::paint(ox::Canvas c) {
    const ox::Brush& brush = enabled_ ? brush_ : disabledBrush_;
    for (int x = 0; x < c.size.width; ++x) {
        c[{.x = x, .y = 0}] = ox::Glyph{.symbol = (x == 0) ? symbol_ : U' ', .brush = brush};
    }
}

void ScrollArrowButton::mouse_press(ox::Mouse mouse) {
    if (enabled_ && mouse.button == ox::Mouse::Button::Left && onClick_) {
        onClick_();
        repeatTimer_.start();
    }
}

void ScrollArrowButton::mouse_release(ox::Mouse) {
    repeatTimer_.stop();
}

void ScrollArrowButton::mouse_leave() {
    repeatTimer_.stop();
}

void ScrollArrowButton::timer() {
    if (enabled_ && onClick_) {
        onClick_();
    }
    else {
        repeatTimer_.stop(); // became disabled (or lost its callback) mid-hold
    }
}

} // namespace ned::ui
