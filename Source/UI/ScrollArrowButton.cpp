#include "ScrollArrowButton.h"

#include "Text/Utf8.h"

namespace ned::ui {

namespace {

    // Fast enough to feel like held-key repeat, slow enough not to blow past
    // the target line while the user reacts and releases. A single fixed
    // interval throughout (no separate, longer "initial delay" before the
    // first repeat) -- a deliberately simple choice over a two-phase or
    // accelerating scheme, matching the size of what was actually asked for.
    constexpr ftxui::animation::Duration kRepeatInterval{0.120F};

} // namespace

ScrollArrowButton::ScrollArrowButton(char32_t symbol, const Brush& brush, const Brush& disabledBrush)
    : symbol_(symbol), brush_(brush), disabledBrush_(disabledBrush) {}

void ScrollArrowButton::SetOnClick(std::function<void()> onClick) {
    onClick_ = std::move(onClick);
}

void ScrollArrowButton::SetEnabled(bool enabled) {
    enabled_ = enabled;
}

bool ScrollArrowButton::IsRepeating() const {
    return repeating_;
}

void ScrollArrowButton::Paint(Canvas c) {
    const Brush&      brush   = enabled_ ? brush_ : disabledBrush_;
    const std::string encoded = text::EncodeCodepointUtf8(symbol_);
    for (int x = 0; x < c.size().width; ++x) {
        ftxui::Cell& cell = c[{.x = x, .y = 0}];
        cell.character    = (x == 0) ? encoded : " ";
        brush.ApplyTo(cell);
    }
}

bool ScrollArrowButton::OnEvent(ftxui::Event event) {
    if (const auto mouse = LocalMouseEvent(event)) {
        if (enabled_ && mouse->button == ftxui::Mouse::Left && mouse->motion == ftxui::Mouse::Pressed && onClick_) {
            onClick_();
            StartRepeating();
            return true;
        }
    }
    // See this class's own header comment for why this is a plain "was
    // anything released" check, not scoped to this widget's own bounds.
    if (event.is_mouse() && event.mouse().motion == ftxui::Mouse::Released && repeating_) {
        StopRepeating();
        return true;
    }
    return false;
}

void ScrollArrowButton::OnAnimation(ftxui::animation::Params& params) {
    if (!repeating_) {
        return;
    }
    if (!enabled_ || !onClick_) {
        StopRepeating(); // became disabled (or lost its callback) mid-hold
        return;
    }

    accumulated_ += params.duration();
    if (accumulated_ >= kRepeatInterval) {
        accumulated_ = ftxui::animation::Duration{0};
        onClick_();
    }
    ftxui::animation::RequestAnimationFrame();
}

void ScrollArrowButton::StartRepeating() {
    repeating_   = true;
    accumulated_ = ftxui::animation::Duration{0};
    ftxui::animation::RequestAnimationFrame();
}

void ScrollArrowButton::StopRepeating() {
    repeating_ = false;
}

} // namespace ned::ui
