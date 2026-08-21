//
// OverlayHost (Source/UI/Overlay.h) -- the floating-widget layer's own
// contract, exercised headlessly with a recording fake: placement/reflow
// boxing, painter's-algorithm z-order over pre-existing Screen content,
// topmost-first mouse interception, and the focus-return-on-hide handshake.
//

#include <catch2/catch_test_macros.hpp>

#include "TestEvents.h"
#include "UI/Overlay.h"
#include "UI/Widget.h"

namespace {

using ned::ui::Box;
using ned::ui::Canvas;
using ned::ui::Event;
using ned::ui::MouseEvent;
using ned::ui::OverlayHost;
using ned::ui::Screen;
using ned::ui::Size;
using ned::ui::Widget;

class FakeOverlay : public Widget {
  public:
    explicit FakeOverlay(std::string fill) : fill_(std::move(fill)) {
    }

    void Paint(Canvas canvas) override {
        ++paintCount;
        for (int y = 0; y < canvas.size().height; ++y) {
            for (int x = 0; x < canvas.size().width; ++x) {
                canvas[{.x = x, .y = y}].character = fill_;
            }
        }
    }

    bool OnEvent(const Event& event) override {
        if (const auto mouse = LocalMouseEvent(event)) {
            localEvents.push_back(*mouse);
            return true;
        }
        return false;
    }

    [[nodiscard]] bool Focusable() const override {
        return true;
    }

    int                     paintCount = 0;
    std::vector<MouseEvent> localEvents;

  private:
    std::string fill_;
};

ned::ui::Event MousePress(int x, int y) {
    return ned::ui::test::Mouse(x, y, MouseEvent::Button::Left, MouseEvent::Motion::Pressed);
}

} // namespace

TEST_CASE("OverlayHost overlays start hidden and Reflow boxes only visible ones", "[Overlay]") {
    OverlayHost host;
    FakeOverlay overlay("X");

    host.Add(overlay, [](Size size) { return Box{.x_min = 0, .x_max = size.width - 1, .y_min = size.height - 3, .y_max = size.height - 1}; });

    REQUIRE_FALSE(host.IsVisible(overlay));
    REQUIRE_FALSE(overlay.active);

    host.Reflow(Size{.width = 20, .height = 10});
    REQUIRE(overlay.size().width == 0); // still hidden -- no box assigned

    host.Show(overlay);
    REQUIRE(host.IsVisible(overlay));
    REQUIRE(overlay.Box_().y_min == 7);
    REQUIRE(overlay.Box_().y_max == 9);
    REQUIRE(overlay.size().width == 20);
}

TEST_CASE("OverlayHost::Show re-boxes from the size cached across a hidden resize", "[Overlay]") {
    OverlayHost host;
    FakeOverlay overlay("X");
    host.Add(overlay, [](Size size) { return Box{.x_min = 0, .x_max = size.width - 1, .y_min = 0, .y_max = 0}; });

    host.Reflow(Size{.width = 30, .height = 10});
    host.Reflow(Size{.width = 50, .height = 12}); // resized while hidden

    host.Show(overlay);
    REQUIRE(overlay.size().width == 50);
}

TEST_CASE("OverlayHost::Paint overpaints prior screen content, hidden overlays excluded", "[Overlay]") {
    OverlayHost host;
    FakeOverlay overlay("X");
    host.Add(overlay, [](Size) { return Box{.x_min = 2, .x_max = 5, .y_min = 1, .y_max = 2}; });
    host.Reflow(Size{.width = 10, .height = 4});

    Screen screen(10, 4);
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 10; ++x) {
            screen.PixelAt(x, y).character = "b";
        }
    }

    host.Paint(screen);
    REQUIRE(overlay.paintCount == 0);
    REQUIRE(screen.PixelAt(3, 1).character == "b");

    host.Show(overlay);
    host.Paint(screen);
    REQUIRE(overlay.paintCount == 1);
    REQUIRE(screen.PixelAt(2, 1).character == "X");
    REQUIRE(screen.PixelAt(5, 2).character == "X");
    REQUIRE(screen.PixelAt(1, 1).character == "b"); // outside the overlay's Box
    REQUIRE(screen.PixelAt(6, 1).character == "b");
}

TEST_CASE("OverlayHost::OnMouseEvent consumes only hits inside a visible overlay", "[Overlay]") {
    OverlayHost host;
    FakeOverlay overlay("X");
    host.Add(overlay, [](Size) { return Box{.x_min = 2, .x_max = 5, .y_min = 1, .y_max = 2}; });
    host.Reflow(Size{.width = 10, .height = 4});

    // Hidden: nothing is intercepted.
    REQUIRE_FALSE(host.OnMouseEvent(MousePress(3, 1)));

    host.Show(overlay);

    REQUIRE(host.OnMouseEvent(MousePress(3, 1)));
    REQUIRE(overlay.localEvents.size() == 1);
    REQUIRE(overlay.localEvents.front().at.x == 1); // translated by the overlay's own Box
    REQUIRE(overlay.localEvents.front().at.y == 0);

    REQUIRE_FALSE(host.OnMouseEvent(MousePress(0, 0))); // outside
    REQUIRE(overlay.localEvents.size() == 1);

    // Keyboard is never intercepted -- focus routing already covers it.
    REQUIRE_FALSE(host.OnMouseEvent(ned::ui::test::Character('a')));
}

TEST_CASE("OverlayHost delivers overlapping hits to the topmost overlay only", "[Overlay]") {
    OverlayHost host;
    FakeOverlay lower("L");
    FakeOverlay upper("U");
    host.Add(lower, [](Size) { return Box{.x_min = 0, .x_max = 5, .y_min = 0, .y_max = 2}; });
    host.Add(upper, [](Size) { return Box{.x_min = 2, .x_max = 7, .y_min = 0, .y_max = 2}; });
    host.Reflow(Size{.width = 10, .height = 4});
    host.Show(lower);
    host.Show(upper); // shown last -- topmost

    REQUIRE(host.OnMouseEvent(MousePress(3, 1))); // inside both
    REQUIRE(upper.localEvents.size() == 1);
    REQUIRE(lower.localEvents.empty());

    Screen screen(10, 4);
    host.Paint(screen);
    REQUIRE(screen.PixelAt(3, 1).character == "U");
    REQUIRE(screen.PixelAt(1, 1).character == "L");

    // Re-showing the lower overlay raises it above.
    host.Show(lower);
    REQUIRE(host.OnMouseEvent(MousePress(3, 1)));
    REQUIRE(lower.localEvents.size() == 1);
}

TEST_CASE("OverlayHost::Hide runs the focus-return callback only for the focus holder", "[Overlay]") {
    OverlayHost host;
    FakeOverlay overlay("X");
    host.Add(overlay, [](Size) { return Box{.x_min = 0, .x_max = 3, .y_min = 0, .y_max = 1}; });
    host.Reflow(Size{.width = 10, .height = 4});

    int focusReturns = 0;
    host.SetFocusReturn(overlay, [&focusReturns] { ++focusReturns; });

    // Hidden without ever being focused: no callback.
    host.Show(overlay);
    host.Hide(overlay);
    REQUIRE(focusReturns == 0);

    host.Show(overlay);
    overlay.TakeFocus();
    host.Hide(overlay);
    REQUIRE(focusReturns == 1);
    REQUIRE_FALSE(host.IsVisible(overlay));

    // Hiding an already-hidden overlay is a no-op, not a second callback.
    host.Hide(overlay);
    REQUIRE(focusReturns == 1);
}
