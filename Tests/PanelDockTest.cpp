//
// PanelDock (Source/UI/PanelDock.h) -- the shared tab strip TerminalPanel/
// AcpPanel (bottom-docked)/DebugConsolePanel now sit behind, exercised
// headlessly against small fake panels rather than the three real (much
// heavier) widgets: tab switching by click and by SwitchTo, active-tab-only
// content delegation, extra-actions scoped to the active tab, maximize/close
// chrome, and resize-drag routing to whichever tab is active.
//

#include <catch2/catch_test_macros.hpp>

#include "TestEvents.h"
#include "UI/PanelDock.h"
#include "UI/Widget.h"

namespace {

using ned::ui::Box;
using ned::ui::Canvas;
using ned::ui::Event;
using ned::ui::MouseEvent;
using ned::ui::PanelDock;
using ned::ui::Screen;
using ned::ui::Size;
using ned::ui::Theme;
using ned::ui::Widget;

class FakePanel : public Widget {
  public:
    explicit FakePanel(std::string fill) : fill_(std::move(fill)) {
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

constexpr int kWidth  = 40;
constexpr int kHeight = 8;

struct Fixture {
    Theme     theme = ned::ui::DarkTheme();
    PanelDock dock{theme};
    FakePanel terminal{"T"};
    FakePanel claude{"C"};
    Screen    screen{kWidth, kHeight};
    int       terminalPercent = 25;
    int       claudePercent   = 40;

    Fixture() {
        dock.SetBox_(Box{.x_min = 0, .x_max = kWidth - 1, .y_min = 0, .y_max = kHeight - 1});
        dock.SetTerminalSize(Size{.width = kWidth, .height = 100});
    }

    void Paint() {
        dock.Paint(Canvas(screen, dock.Box_()));
    }

    [[nodiscard]] std::string RowText(int y) {
        std::string text;
        for (int x = 0; x < kWidth; ++x) {
            text += screen.PixelAt(x, y).character;
        }
        while (!text.empty() && text.back() == ' ') {
            text.pop_back();
        }
        return text;
    }
};

Event MousePress(int x, int y) {
    return ned::ui::test::Mouse(x, y, MouseEvent::Button::Left, MouseEvent::Motion::Pressed);
}
Event MouseMove(int x, int y) {
    return ned::ui::test::Mouse(x, y, MouseEvent::Button::None, MouseEvent::Motion::Moved);
}
Event MouseRelease(int x, int y) {
    return ned::ui::test::Mouse(x, y, MouseEvent::Button::Left, MouseEvent::Motion::Released);
}

} // namespace

TEST_CASE("PanelDock paints only the active tab's content, below the shared tab strip", "[PanelDock]") {
    Fixture f;
    f.dock.AddPanel("Terminal", f.terminal);
    f.dock.AddPanel("Claude", f.claude);
    f.Paint();

    REQUIRE(f.terminal.paintCount == 1);
    REQUIRE(f.claude.paintCount == 0);
    REQUIRE(f.screen.PixelAt(0, 0).character == "─"); // tab-strip row
    REQUIRE(f.screen.PixelAt(5, 1).character == "T"); // content row, active tab
    REQUIRE(f.RowText(0).find("Terminal") != std::string::npos);
}

TEST_CASE("PanelDock::SwitchTo repositions and focuses the newly active panel", "[PanelDock]") {
    Fixture f;
    f.dock.AddPanel("Terminal", f.terminal);
    f.dock.AddPanel("Claude", f.claude);

    f.dock.SwitchTo(1);
    REQUIRE(f.dock.ActiveIndex() == 1);
    REQUIRE(f.dock.ActivePanel() == &f.claude);
    REQUIRE(f.claude.Focused());
    REQUIRE(f.claude.Box_().y_min == 1); // below the tab-strip row

    f.Paint();
    REQUIRE(f.claude.paintCount == 1);
    REQUIRE(f.terminal.paintCount == 0); // never active, never painted

    // Out-of-range clamps rather than crashing.
    f.dock.SwitchTo(99);
    REQUIRE(f.dock.ActiveIndex() == 1);
}

TEST_CASE("PanelDock clicking a tab label switches to it", "[PanelDock]") {
    Fixture f;
    f.dock.AddPanel("Terminal", f.terminal);
    f.dock.AddPanel("Claude", f.claude);
    f.Paint();

    // Layout: "[ Terminal ]" (active, 12 columns) at x=[1,13), then a
    // 1-column gap, then " Claude " (inactive, 8 columns) at x=[14,22) --
    // any column inside that second span lands on the Claude tab.
    REQUIRE(f.dock.OnEvent(MousePress(16, 0)));
    REQUIRE(f.dock.ActiveIndex() == 1);
    REQUIRE(f.claude.Focused());
}

TEST_CASE("PanelDock forwards non-tab-strip mouse events to the active panel only", "[PanelDock]") {
    Fixture f;
    f.dock.AddPanel("Terminal", f.terminal);
    f.dock.AddPanel("Claude", f.claude);
    f.Paint();

    REQUIRE(f.dock.OnEvent(MousePress(5, 3)));
    REQUIRE(f.terminal.localEvents.size() == 1);
    REQUIRE(f.terminal.localEvents.front().at.y == 2); // translated against terminal's own Box (y_min = 1)
    REQUIRE(f.claude.localEvents.empty());
}

TEST_CASE("PanelDock extra actions are only reachable while their own tab is active", "[PanelDock]") {
    Fixture f;
    int     searchClicks = 0;
    f.dock.AddPanel("Terminal", f.terminal, {}, {}, {}, [&searchClicks] {
        return std::vector<PanelDock::TabAction>{{.icon = U'/', .onClick = [&searchClicks] { ++searchClicks; }}};
    });
    f.dock.AddPanel("Claude", f.claude);
    f.Paint();

    // Rightmost chrome, active tab (Terminal, which has an extra action):
    // [/][▲][x], 3 buttons -- the search icon is the leftmost of the three.
    const int searchButtonX = kWidth - 3 * 4 + 1;
    REQUIRE(f.dock.OnEvent(MousePress(searchButtonX, 0)));
    REQUIRE(searchClicks == 1);

    // Claude has no extra actions -- the cluster shrinks to [▲][x] (2
    // buttons) and shifts right, so the old search column now falls before
    // the (moved) cluster entirely, i.e. it's simply unclaimed.
    f.dock.SwitchTo(1);
    searchClicks = 0;
    REQUIRE(f.dock.OnEvent(MousePress(searchButtonX, 0)));
    REQUIRE(searchClicks == 0);
    REQUIRE_FALSE(f.dock.Maximized());

    // The real maximize button, at its own (now 2-button-cluster) position.
    const int maximizeButtonX = kWidth - 2 * 4 + 1;
    REQUIRE(f.dock.OnEvent(MousePress(maximizeButtonX, 0)));
    REQUIRE(f.dock.Maximized());
}

TEST_CASE("PanelDock maximize toggles and fires the layout-change hook", "[PanelDock]") {
    Fixture f;
    f.dock.AddPanel("Terminal", f.terminal);
    int layoutChanges = 0;
    f.dock.SetOnLayoutChange([&layoutChanges] { ++layoutChanges; });
    f.Paint();

    REQUIRE_FALSE(f.dock.Maximized());
    const int maximizeButtonX = kWidth - 2 * 4 + 1; // [▲] then [x] -- maximize is second-to-last
    REQUIRE(f.dock.OnEvent(MousePress(maximizeButtonX, 0)));
    REQUIRE(f.dock.Maximized());
    REQUIRE(layoutChanges == 1);
}

TEST_CASE("PanelDock::SwitchTo fires the layout-change hook so the outer box is re-derived", "[PanelDock]") {
    // Live-pty-confirmed bug this guards against: OverlayHost only
    // recomputes a widget's own Box_ on Show()/Reflow(), never per-event --
    // without SwitchTo notifying the layout-change hook, switching from a
    // tall tab to a short one (or vice versa) left the dock at the
    // previously-active tab's own height until the next real terminal
    // resize, whether the switch came from a click or a toggle command.
    Fixture f;
    f.dock.AddPanel("Terminal", f.terminal);
    f.dock.AddPanel("Claude", f.claude);
    int layoutChanges = 0;
    f.dock.SetOnLayoutChange([&layoutChanges] { ++layoutChanges; });

    f.dock.SwitchTo(1);
    REQUIRE(layoutChanges == 1);

    // Switching to the tab that's already active is a no-op -- nothing
    // about the dock's own sizing could have changed.
    f.dock.SwitchTo(1);
    REQUIRE(layoutChanges == 1);

    f.dock.SwitchTo(0);
    REQUIRE(layoutChanges == 2);
}

TEST_CASE("PanelDock close button fires the close-request hook", "[PanelDock]") {
    Fixture f;
    f.dock.AddPanel("Terminal", f.terminal);
    int closes = 0;
    f.dock.SetOnCloseRequest([&closes] { ++closes; });
    f.Paint();

    const int closeButtonX = kWidth - 1 * 4 + 1;
    REQUIRE(f.dock.OnEvent(MousePress(closeButtonX, 0)));
    REQUIRE(closes == 1);
}

TEST_CASE("PanelDock drag-resize on an unclaimed tab-strip column adjusts the active tab's own percent", "[PanelDock]") {
    Fixture f;
    f.dock.AddPanel(
        "Terminal", f.terminal, {}, [&f] { return f.terminalPercent; }, [&f](int p) { f.terminalPercent = p; });
    f.dock.AddPanel(
        "Claude", f.claude, {}, [&f] { return f.claudePercent; }, [&f](int p) { f.claudePercent = p; });
    f.Paint();

    // Column 25: both tab labels end by column 22 (whichever is active --
    // 12+8 or 10+10 columns either way), and neither tab registers extra
    // actions, so the button cluster (2 buttons) starts at column 32 --
    // column 25 is unclaimed in both phases below, so the press starts a
    // resize instead of switching tabs. The press must land on row 0 (the
    // tab strip) to arm the drag at all; the resulting delta is then read
    // off the *global* mouse position on every Moved event regardless of
    // row, TerminalPanel/AcpPanel's own "anchor minus current" convention --
    // moving to a smaller y (up, off the top of the dock is fine for a
    // synthetic drag) grows a bottom-docked panel.
    REQUIRE(f.dock.OnEvent(MousePress(25, 0)));
    REQUIRE(f.dock.OnEvent(MouseMove(25, -50))); // dragged 50px up -> +50%
    REQUIRE(f.terminalPercent == 75);
    REQUIRE(f.dock.OnEvent(MouseRelease(25, -50)));

    f.dock.SwitchTo(1);
    f.Paint();
    REQUIRE(f.dock.OnEvent(MousePress(25, 0)));
    REQUIRE(f.dock.OnEvent(MouseMove(25, -30))); // +30%
    REQUIRE(f.claudePercent == 70);
    REQUIRE(f.terminalPercent == 75); // untouched by the second drag
}

TEST_CASE("PanelDock resize is a no-op for a tab with no percent pair registered", "[PanelDock]") {
    Fixture f;
    f.dock.AddPanel("Debug console", f.terminal); // no getPercent/setPercent
    f.Paint();

    REQUIRE(f.dock.OnEvent(MousePress(20, 0)));
    REQUIRE(f.dock.OnEvent(MouseMove(20, 50)));
    REQUIRE(f.dock.OnEvent(MouseRelease(20, 50)));
    // Nothing to assert beyond "doesn't crash" -- there's no percent pair to
    // have changed.
}
