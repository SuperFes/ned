//
// PanelDock (Source/UI/PanelDock.h) -- the shared tab strip TerminalPanel/
// AcpPanel (bottom-docked)/DebugConsolePanel now sit behind, exercised
// headlessly against small fake panels rather than the three real (much
// heavier) widgets: tab switching by click and by SwitchTo, active-tab-only
// content delegation, extra-actions scoped to the active tab, maximize/
// restore/hide-dock chrome (tab-glyph-redesign follow-up: no bracket
// characters anywhere in this row, and three distinct glyphs for tab-close/
// maximize/hide-dock), a shared height (Percent()) that switching tabs never
// changes (tab-height-unification follow-up), and resize-drag routing to
// whichever tab is active.
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
Event MouseWheel(int x, int y, MouseEvent::Button button) {
    return ned::ui::test::Mouse(x, y, button, MouseEvent::Motion::Pressed);
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

    // tab-glyph-redesign follow-up: no brackets, active/inactive is a color
    // distinction now, so every tab's own label is the same padded text
    // regardless of which is active. Layout: " Terminal " (10 columns) + its
    // own end-cap column at x=[1,12), then " Claude " (8 columns) + end-cap
    // at x=[12,21) -- any column inside that second span lands on Claude.
    REQUIRE(f.dock.OnEvent(MousePress(16, 0)));
    REQUIRE(f.dock.ActiveIndex() == 1);
    REQUIRE(f.claude.Focused());
}

TEST_CASE("PanelDock tab strip has no bracket characters anywhere -- color marks the active tab instead",
          "[PanelDock]") {
    // The literal bug report this guards against: "[ ]" used to mark both
    // the active tab's own label AND every button in the right-side
    // cluster, "the tabs don't really feel tabby ... we use the same chars
    // [] for two different parts of the tabbed interface."
    Fixture f;
    f.dock.AddPanel("Terminal", f.terminal);
    f.dock.AddPanel("Claude", f.claude);
    f.Paint();

    const std::string row = f.RowText(0);
    REQUIRE(row.find('[') == std::string::npos);
    REQUIRE(row.find(']') == std::string::npos);
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
    // search icon, then maximize, then hide-dock -- 3 buttons, no brackets
    // around any of them -- the search icon is the leftmost of the three.
    const int searchButtonX = kWidth - 3 * 4 + 1;
    REQUIRE(f.dock.OnEvent(MousePress(searchButtonX, 0)));
    REQUIRE(searchClicks == 1);

    // Claude has no extra actions -- the cluster shrinks to maximize +
    // hide-dock (2 buttons) and shifts right, so the old search column now
    // falls before the (moved) cluster entirely, i.e. it's simply unclaimed.
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
    const int maximizeButtonX = kWidth - 2 * 4 + 1;  // maximize, then hide-dock -- maximize is second-to-last
    const int maximizeIconX   = maximizeButtonX + 1; // the icon sits one column past the button's own leading pad
    REQUIRE(f.screen.PixelAt(maximizeIconX, 0).character == "▲");
    REQUIRE(f.dock.OnEvent(MousePress(maximizeButtonX, 0)));
    REQUIRE(f.dock.Maximized());
    REQUIRE(layoutChanges == 1);

    // tab-glyph-redesign follow-up: the maximize glyph itself now flips to
    // ▼ once actually maximized -- a real bug fixed alongside the redesign,
    // the button used to always paint ▲ regardless of Maximized() state.
    f.Paint();
    REQUIRE(f.screen.PixelAt(maximizeIconX, 0).character == "▼");
}

TEST_CASE("PanelDock::SwitchTo never changes the dock's own shared height", "[PanelDock]") {
    // tab-height-unification follow-up: this is the exact inverse of what
    // this class used to guarantee. Before, switching tabs deliberately
    // fired the layout-change hook because each tab could resolve a
    // different ActivePercent() -- a real, reported "since they're grouped
    // together the heights between tabs should be the same" complaint about
    // that behavior. Now Percent() is one value shared by every tab, so a
    // plain SwitchTo has nothing sizing-related left to notify about, even
    // between two tabs with very different persisted percents.
    Fixture f;
    f.dock.AddPanel(
        "Terminal", f.terminal, {}, [&f] { return f.terminalPercent; }, [&f](int p) { f.terminalPercent = p; });
    f.dock.AddPanel(
        "Claude", f.claude, {}, [&f] { return f.claudePercent; }, [&f](int p) { f.claudePercent = p; });
    int layoutChanges = 0;
    f.dock.SetOnLayoutChange([&layoutChanges] { ++layoutChanges; });

    REQUIRE(f.terminalPercent != f.claudePercent); // fixture defaults: 25 vs. 40
    const int before = f.dock.Percent();

    f.dock.SwitchTo(1);
    REQUIRE(f.dock.Percent() == before);
    REQUIRE(layoutChanges == 0);

    f.dock.SwitchTo(0);
    REQUIRE(f.dock.Percent() == before);
    REQUIRE(layoutChanges == 0);
}

TEST_CASE("PanelDock close button fires the close-request hook", "[PanelDock]") {
    Fixture f;
    f.dock.AddPanel("Terminal", f.terminal);
    int closes = 0;
    f.dock.SetOnCloseRequest([&closes] { ++closes; });
    f.Paint();

    const int closeButtonX = kWidth - 1 * 4 + 1;
    // tab-glyph-redesign follow-up: the shared hide-dock button paints ▾,
    // not the × a per-tab close action uses -- see the test below for the
    // full distinct-glyph regression. The icon sits one column past the
    // button's own leading pad column.
    REQUIRE(f.screen.PixelAt(closeButtonX + 1, 0).character == "▾");
    REQUIRE(f.dock.OnEvent(MousePress(closeButtonX, 0)));
    REQUIRE(closes == 1);
}

TEST_CASE("PanelDock hide-dock button uses a different glyph than a per-tab close action", "[PanelDock]") {
    // The exact bug reported live: "I get that one of the x's for the
    // terminal just closes the terminal and not the bottom tab bar, but it
    // should not be the same glyph in both places." Terminal's own
    // close-this-tab TabAction (main.cpp) uses × deliberately -- this test
    // pins that the shared hide-the-whole-dock button never reuses it.
    Fixture f;
    int     tabCloseClicks = 0;
    f.dock.AddPanel("Terminal", f.terminal, {}, {}, {}, [&tabCloseClicks] {
        return std::vector<PanelDock::TabAction>{{.icon = U'×', .onClick = [&tabCloseClicks] { ++tabCloseClicks; }}};
    });
    int dockCloses = 0;
    f.dock.SetOnCloseRequest([&dockCloses] { ++dockCloses; });
    f.Paint();

    // Rightmost chrome: tab-close (×), maximize (▲), hide-dock (▾) -- three
    // distinct buttons, three distinct glyphs, none of them brackets. Each
    // button's own icon sits one column past its leading pad column.
    const int tabCloseButtonX = kWidth - 3 * 4 + 1;
    const int hideDockButtonX = kWidth - 1 * 4 + 1;
    REQUIRE(f.screen.PixelAt(tabCloseButtonX + 1, 0).character == "×");
    REQUIRE(f.screen.PixelAt(hideDockButtonX + 1, 0).character == "▾");

    REQUIRE(f.dock.OnEvent(MousePress(tabCloseButtonX, 0)));
    REQUIRE(tabCloseClicks == 1);
    REQUIRE(dockCloses == 0); // the per-tab close never hides the whole dock

    REQUIRE(f.dock.OnEvent(MousePress(hideDockButtonX, 0)));
    REQUIRE(dockCloses == 1);
    REQUIRE(tabCloseClicks == 1); // and the hide-dock button never closes the tab
}

TEST_CASE("PanelDock drag-resize writes through the active tab's own percent pair, anchored on the shared height",
          "[PanelDock]") {
    Fixture f;
    f.dock.AddPanel(
        "Terminal", f.terminal, {}, [&f] { return f.terminalPercent; }, [&f](int p) { f.terminalPercent = p; });
    f.dock.AddPanel(
        "Claude", f.claude, {}, [&f] { return f.claudePercent; }, [&f](int p) { f.claudePercent = p; });
    f.Paint();

    // Column 25: both tab labels end well before column 25 either way, and
    // neither tab registers extra actions, so the button cluster (2
    // buttons) starts at column 32 -- column 25 is unclaimed in both phases
    // below, so the press starts a resize instead of switching tabs. The
    // press must land on row 0 (the tab strip) to arm the drag at all; the
    // resulting delta is then read off the *global* mouse position on every
    // Moved event regardless of row, TerminalPanel/AcpPanel's own "anchor
    // minus current" convention -- moving to a smaller y (up, off the top
    // of the dock is fine for a synthetic drag) grows a bottom-docked panel.
    REQUIRE(f.dock.OnEvent(MousePress(25, 0)));
    REQUIRE(f.dock.OnEvent(MouseMove(25, -50))); // dragged 50px up -> +50%
    REQUIRE(f.terminalPercent == 75);            // 25 (Terminal's own starting percent) + 50
    REQUIRE(f.dock.Percent() == 75);
    REQUIRE(f.dock.OnEvent(MouseRelease(25, -50)));

    // tab-height-unification follow-up: switching to Claude and dragging
    // again anchors on the dock's own *shared* height (75, just set above),
    // not Claude's own independently-persisted 40 -- Claude's percent pair
    // still receives the write (its own setting still persists correctly),
    // but the starting point for the drag is whatever height was actually
    // on screen, matching what the user just saw and dragged from.
    f.dock.SwitchTo(1);
    f.Paint();
    REQUIRE(f.dock.OnEvent(MousePress(25, 0)));
    REQUIRE(f.dock.OnEvent(MouseMove(25, -30))); // +30% off the shared 75, not off claudePercent's own 40
    REQUIRE(f.claudePercent == 105);
    REQUIRE(f.dock.Percent() == 105);
    REQUIRE(f.terminalPercent == 75); // untouched by the second drag
}

TEST_CASE("PanelDock resize on a tab with no percent pair still moves the shared height, just doesn't persist it",
          "[PanelDock]") {
    // tab-height-unification follow-up: unlike the old per-tab design (where
    // this really was a no-op, since there was nowhere to write the result),
    // dragging while DebugConsole/Janet-REPL-shaped tab (no getPercent/
    // setPercent at all) is active still resizes the dock live -- it's
    // Percent() that's shared now, not just the persisted setting -- it
    // simply has nothing to write through to disk once released.
    Fixture f;
    f.dock.AddPanel("Debug console", f.terminal); // no getPercent/setPercent
    f.Paint();

    REQUIRE(f.dock.Percent() == 30); // default seed -- no entry ever registered a getPercent to seed from
    REQUIRE(f.dock.OnEvent(MousePress(20, 0)));
    REQUIRE(f.dock.OnEvent(MouseMove(20, 50))); // dragged 50px down -> -50%, clamped
    REQUIRE(f.dock.Percent() == 10);            // 30 - 50 clamped to the [10, 90] floor
    REQUIRE(f.dock.OnEvent(MouseRelease(20, 50)));
}

TEST_CASE("PanelDock::Percent seeds from the first tab that registers a getPercent, not necessarily the active one",
          "[PanelDock]") {
    Fixture f;
    f.dock.AddPanel("Debug console", f.terminal); // no getPercent -- added first, but never seeds anything
    f.dock.AddPanel(
        "Terminal", f.claude, {}, [&f] { return f.terminalPercent; }, [&f](int p) { f.terminalPercent = p; });

    REQUIRE(f.dock.Percent() == f.terminalPercent); // 25, the fixture's own default
}

TEST_CASE("PanelDock::RemovePanel drops a non-active tab without disturbing the active one", "[PanelDock]") {
    Fixture    f;
    FakePanel  repl{"R"};
    const auto terminalId = f.dock.AddPanel("Terminal", f.terminal);
    const auto claudeId   = f.dock.AddPanel("Claude", f.claude);
    f.dock.AddPanel("Repl", repl);
    int layoutChanges = 0;
    f.dock.SetOnLayoutChange([&layoutChanges] { ++layoutChanges; });

    f.dock.RemovePanel(claudeId);
    REQUIRE(f.dock.PanelCount() == 2);
    REQUIRE(f.dock.ActiveIndex() == terminalId);
    REQUIRE(layoutChanges == 0); // the active tab's own identity never changed

    f.Paint();
    REQUIRE(f.RowText(0).find("Claude") == std::string::npos);
    REQUIRE(f.RowText(0).find("Repl") != std::string::npos);
}

TEST_CASE("PanelDock::RemovePanel on the active tab promotes its neighbor and reveals it", "[PanelDock]") {
    Fixture    f;
    FakePanel  repl{"R"};
    const auto terminalId    = f.dock.AddPanel("Terminal", f.terminal);
    const auto claudeId      = f.dock.AddPanel("Claude", f.claude);
    const auto replId        = f.dock.AddPanel("Repl", repl);
    int        layoutChanges = 0;
    f.dock.SetOnLayoutChange([&layoutChanges] { ++layoutChanges; });

    f.dock.SwitchTo(claudeId);
    layoutChanges = 0;
    f.dock.RemovePanel(claudeId);

    // Claude sat between Terminal and Repl -- removing the active middle
    // tab promotes whatever now occupies that same screen position, i.e.
    // Repl (Claude's old position now holds Repl after the erase).
    REQUIRE(f.dock.ActiveIndex() == replId);
    REQUIRE(f.claude.Focused() == false);
    REQUIRE(f.dock.ActivePanel() == &repl);
    REQUIRE(layoutChanges == 1);

    // Removing the id a second time is a safe no-op.
    f.dock.RemovePanel(claudeId);
    REQUIRE(f.dock.PanelCount() == 2);
    REQUIRE(f.dock.ActiveIndex() == replId);

    // A stale id (already removed) can no longer be switched to.
    f.dock.SwitchTo(terminalId);
    REQUIRE(f.dock.ActiveIndex() == terminalId);
    f.dock.SwitchTo(claudeId);
    REQUIRE(f.dock.ActiveIndex() == terminalId); // unchanged -- claudeId is gone
}

TEST_CASE("PanelDock::RemovePanel down to the last tab leaves it active", "[PanelDock]") {
    Fixture    f;
    const auto terminalId = f.dock.AddPanel("Terminal", f.terminal);
    const auto claudeId   = f.dock.AddPanel("Claude", f.claude);

    f.dock.RemovePanel(terminalId);
    REQUIRE(f.dock.PanelCount() == 1);
    REQUIRE(f.dock.ActiveIndex() == claudeId);
    REQUIRE(f.dock.ActivePanel() == &f.claude);
}

TEST_CASE("PanelDock tab strip scrolls with the mouse wheel and shows overflow indicators", "[PanelDock]") {
    // A narrow dock (kWidth==40) with several long-labeled tabs overflows
    // the label region -- multiple-terminal-tabs follow-up's whole reason
    // for existing: many terminal tabs plus the fixed Claude/Repl tabs can
    // easily exceed a real terminal's width.
    Fixture   f;
    FakePanel t2{"2"};
    FakePanel t3{"3"};
    FakePanel t4{"4"};
    f.dock.AddPanel("Terminal", f.terminal);
    f.dock.AddPanel("Terminal <2>", t2);
    f.dock.AddPanel("Terminal <3>", t3);
    const auto lastId = f.dock.AddPanel("Terminal <4>", t4);
    f.Paint();

    // No left indicator yet (nothing scrolled past), but the row overflows
    // to the right.
    REQUIRE(f.screen.PixelAt(0, 0).character != "‹");
    const bool anyRightIndicator = [&] {
        for (int x = 0; x < kWidth; ++x) {
            if (f.screen.PixelAt(x, 0).character == "›") {
                return true;
            }
        }
        return false;
    }();
    REQUIRE(anyRightIndicator);

    // Wheel down over the tab strip scrolls it right.
    REQUIRE(f.dock.OnEvent(MouseWheel(5, 0, MouseEvent::Button::WheelDown)));
    f.Paint();
    REQUIRE(f.screen.PixelAt(0, 0).character == "‹");

    // Switching to the last (currently scrolled-past) tab reveals it --
    // its own label must appear somewhere on the row afterward.
    f.dock.SwitchTo(lastId);
    f.Paint();
    REQUIRE(f.RowText(0).find("Terminal <4>") != std::string::npos);
}
