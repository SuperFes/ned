#include "ScrollArrowButton.h"

#include <chrono>

#include "Text/Utf8.h"

namespace ned::ui {

namespace {

    // Fast enough to feel like held-key repeat, slow enough not to blow past
    // the target line while the user reacts and releases. A single fixed
    // interval throughout (no separate, longer "initial delay" before the
    // first repeat) -- a deliberately simple choice over a two-phase or
    // accelerating scheme, matching the size of what was actually asked for.
    constexpr std::chrono::milliseconds kRepeatInterval{120};

} // namespace

ScrollArrowButton::ScrollArrowButton(char32_t symbol, const Brush& brush, const Brush& disabledBrush) : symbol_(symbol), brush_(brush), disabledBrush_(disabledBrush) {
}

ScrollArrowButton::~ScrollArrowButton() {
    StopRepeating();
}

void ScrollArrowButton::SetOnClick(std::function<void()> onClick) {
    onClick_ = std::move(onClick);
}

void ScrollArrowButton::SetEnabled(bool enabled) {
    enabled_ = enabled;
}

void ScrollArrowButton::SetEventLoop(EventLoop* loop) {
    loop_ = loop;
}

bool ScrollArrowButton::IsRepeating() const {
    return repeating_;
}

void ScrollArrowButton::Paint(Canvas c) {
    const Brush&      brush   = enabled_ ? brush_ : disabledBrush_;
    const std::string encoded = text::EncodeCodepointUtf8(symbol_);
    for (int x = 0; x < c.size().width; ++x) {
        Cell& cell     = c[{.x = x, .y = 0}];
        cell.character = (x == 0) ? encoded : " ";
        brush.ApplyTo(cell);
    }
}

bool ScrollArrowButton::OnEvent(const Event& event) {
    if (const auto mouse = LocalMouseEvent(event)) {
        if (enabled_ && mouse->button == MouseEvent::Button::Left && mouse->motion == MouseEvent::Motion::Pressed &&
            onClick_) {
            onClick_();
            StartRepeating();
            return true;
        }
    }
    // See this class's own header comment for why this is a plain "was
    // anything released" check, not scoped to this widget's own bounds.
    if (event.is_mouse() && event.mouse().motion == MouseEvent::Motion::Released && repeating_) {
        StopRepeating();
        return true;
    }
    return false;
}

void ScrollArrowButton::StartRepeating() {
    StopRepeating(); // in case a stale thread from a previous hold is still winding down
    repeating_ = true;
    if (loop_ == nullptr) {
        return;
    }
    repeatThread_ = std::jthread([this](const std::stop_token& stopToken) {
        while (!stopToken.stop_requested() && repeating_) {
            std::this_thread::sleep_for(kRepeatInterval);
            if (stopToken.stop_requested() || !repeating_) {
                return;
            }
            loop_->Post([this] {
                if (repeating_ && enabled_ && onClick_) {
                    onClick_();
                }
                else {
                    repeating_ = false;
                }
            });
        }
    });
}

void ScrollArrowButton::StopRepeating() {
    repeating_ = false;
    if (repeatThread_.joinable()) {
        repeatThread_.request_stop();
    }
}

} // namespace ned::ui
