//
// LeftDock (Source/UI/LeftDock.h) -- step 1 of the "Persistent left-side
// glyph rail for toggling panels" ROADMAP entry: the standalone widget,
// exercised headlessly against small fake content widgets rather than the
// real (much heavier) ProjectSidebar/VcsPanel, which aren't retrofitted to
// be hosted by it yet. Mirrors PanelDockTest.cpp's own FakePanel/Fixture
// shape -- LeftDock's own header comment explains why collapse is
// rail-glyph-driven here rather than the divider-double-click convention
// ProjectSidebar/VcsPanel/AcpPanel each separately patched.
//

#include <catch2/catch_test_macros.hpp>

#include "TestEvents.h"
#include "UI/LeftDock.h"
#include "UI/Widget.h"

namespace {

using ned::ui::Box;
using ned::ui::Canvas;
using ned::ui::Event;
using ned::ui::LeftDock;
using ned::ui::MouseEvent;
using ned::ui::Screen;
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

    void OnFocusPreempted() override {
        ++focusPreemptedCount;
    }

    int                     paintCount           = 0;
    int                     focusPreemptedCount  = 0;
    std::vector<MouseEvent> localEvents;

  private:
    std::string fill_;
};

constexpr int kWidth  = 40;
constexpr int kHeight = 10;

struct Fixture {
    Theme     theme = ned::ui::DarkTheme();
    LeftDock  dock{theme};
    FakePanel files{"F"};
    FakePanel vcs{"V"};
    Screen    screen{kWidth, kHeight};

    Fixture() {
        dock.SetBox_(Box{.x_min = 0, .x_max = kWidth - 1, .y_min = 0, .y_max = kHeight - 1});
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

TEST_CASE("LeftDock registers the first panel as active by construction", "[LeftDock]") {
    Fixture           f;
    const std::size_t filesId = f.dock.AddPanel(U'\U000025A4', "Files", f.files);
    f.dock.AddPanel(U'\U000000B1', "VCS", f.vcs);

    REQUIRE(f.dock.ActivePanel() == filesId);
    REQUIRE(f.dock.ActiveContent() == &f.files);
    REQUIRE(f.dock.PanelCount() == 2);
}

TEST_CASE("LeftDock paints the rail plus only the active panel's content", "[LeftDock]") {
    Fixture f;
    f.dock.AddPanel(U'F', "Files", f.files);
    const std::size_t vcsId = f.dock.AddPanel(U'V', "VCS", f.vcs);
    f.Paint();

    REQUIRE(f.files.paintCount == 1);
    REQUIRE(f.vcs.paintCount == 0);

    // Rail: one glyph per row, in registration order.
    REQUIRE(f.screen.PixelAt(1, 0).character == "F");
    REQUIRE(f.screen.PixelAt(1, 1).character == "V");

    // The content region's border carries the active panel's name.
    REQUIRE(f.RowText(0).find("Files") != std::string::npos);

    f.dock.CommitSwitchTo(vcsId);
    f.Paint();
    REQUIRE(f.vcs.paintCount == 1);
    REQUIRE(f.RowText(0).find("VCS") != std::string::npos);
}

TEST_CASE("LeftDock::CommitSwitchTo repositions the newly active content and fires the commit callback", "[LeftDock]") {
    Fixture f;
    f.dock.AddPanel(U'F', "Files", f.files);
    const std::size_t vcsId = f.dock.AddPanel(U'V', "VCS", f.vcs);

    std::optional<std::size_t> committed;
    f.dock.SetOnActivePanelCommitted([&](std::size_t id) { committed = id; });

    f.dock.CommitSwitchTo(vcsId);

    REQUIRE(f.dock.ActivePanel() == vcsId);
    REQUIRE(f.dock.ActiveContent() == &f.vcs);
    REQUIRE(committed == vcsId);
    // Repositioned to the content interior box -- inset by the rail on the
    // left and the border on every side, matching ContentInteriorBox()'s
    // own math against this fixture's Box (x in [0, 39], y in [0, 9]).
    REQUIRE(f.vcs.Box_().x_min == 4);
    REQUIRE(f.vcs.Box_().x_max == 38);
    REQUIRE(f.vcs.Box_().y_min == 1);
    REQUIRE(f.vcs.Box_().y_max == 8);
}

TEST_CASE("SwitchTo repositions content but does not fire the commit callback", "[LeftDock]") {
    Fixture f;
    f.dock.AddPanel(U'F', "Files", f.files);
    const std::size_t vcsId = f.dock.AddPanel(U'V', "VCS", f.vcs);

    bool committed = false;
    f.dock.SetOnActivePanelCommitted([&](std::size_t) { committed = true; });

    f.dock.SwitchTo(vcsId);

    REQUIRE(f.dock.ActivePanel() == vcsId);
    REQUIRE_FALSE(committed);
}

TEST_CASE("A no-op SwitchTo/CommitSwitchTo with an unregistered id changes nothing", "[LeftDock]") {
    Fixture            f;
    const std::size_t  filesId = f.dock.AddPanel(U'F', "Files", f.files);
    f.dock.AddPanel(U'V', "VCS", f.vcs);

    f.dock.SwitchTo(999);
    REQUIRE(f.dock.ActivePanel() == filesId);

    f.dock.CommitSwitchTo(999);
    REQUIRE(f.dock.ActivePanel() == filesId);
}

TEST_CASE("ActivateOrToggle: re-activating the active expanded panel collapses; anything else expands+switches",
          "[LeftDock]") {
    Fixture f;
    f.dock.AddPanel(U'F', "Files", f.files);
    const std::size_t vcsId = f.dock.AddPanel(U'V', "VCS", f.vcs);

    // Same shape as the rail-click test below, driven programmatically --
    // BufferView's toggle-project-sidebar/toggle-vcs-panel commands call
    // this directly instead of simulating a mouse event.
    f.dock.ActivateOrToggle(&f.files); // already active+expanded -> collapses
    REQUIRE(f.dock.Collapsed());

    f.dock.ActivateOrToggle(&f.vcs); // collapsed, different panel -> expands+switches
    REQUIRE_FALSE(f.dock.Collapsed());
    REQUIRE(f.dock.ActivePanel() == vcsId);

    f.dock.ActivateOrToggle(&f.files); // expanded, different panel -> switches only
    REQUIRE_FALSE(f.dock.Collapsed());
    REQUIRE(f.dock.ActivePanel() != vcsId);
}

TEST_CASE("ActivateOrToggle with an unregistered widget is a safe no-op", "[LeftDock]") {
    Fixture   f;
    FakePanel stray{"S"};
    f.dock.AddPanel(U'F', "Files", f.files);

    f.dock.ActivateOrToggle(&stray);
    REQUIRE_FALSE(f.dock.Collapsed());
    REQUIRE(f.dock.ActiveContent() == &f.files);
}

TEST_CASE("PrepareForKeyboardFocus switches to the target panel if a different one is active", "[LeftDock]") {
    Fixture f;
    f.dock.AddPanel(U'F', "Files", f.files);
    const std::size_t vcsId = f.dock.AddPanel(U'V', "VCS", f.vcs);
    f.dock.SwitchTo(vcsId);

    bool activeCommitted = false;
    f.dock.SetOnActivePanelCommitted([&](std::size_t) { activeCommitted = true; });

    // focus-project-sidebar while VCS is active: switches to Files, silently.
    f.dock.PrepareForKeyboardFocus(&f.files);
    REQUIRE(f.dock.ActiveContent() == &f.files);
    REQUIRE_FALSE(f.dock.Collapsed());
    REQUIRE_FALSE(activeCommitted); // a quick keyboard visit doesn't overwrite the remembered active panel
}

TEST_CASE("Clicking the active panel's rail glyph collapses; clicking another expands and switches", "[LeftDock]") {
    Fixture f;
    f.dock.AddPanel(U'F', "Files", f.files); // active by default, rail row 0
    const std::size_t vcsId = f.dock.AddPanel(U'V', "VCS", f.vcs); // rail row 1

    std::optional<bool> collapseCommitted;
    f.dock.SetOnCollapseCommitted([&](bool collapsed) { collapseCommitted = collapsed; });

    REQUIRE(f.dock.OnEvent(MousePress(1, 0))); // active glyph, row 0
    REQUIRE(f.dock.Collapsed());
    REQUIRE(collapseCommitted == true);

    collapseCommitted.reset();
    REQUIRE(f.dock.OnEvent(MousePress(1, 1))); // VCS glyph, row 1, while collapsed
    REQUIRE_FALSE(f.dock.Collapsed());
    REQUIRE(f.dock.ActivePanel() == vcsId);
    REQUIRE(collapseCommitted == false);
}

TEST_CASE("Clicking a different rail glyph while expanded switches without collapsing", "[LeftDock]") {
    Fixture f;
    f.dock.AddPanel(U'F', "Files", f.files);
    const std::size_t vcsId = f.dock.AddPanel(U'V', "VCS", f.vcs);

    bool collapseFired = false;
    f.dock.SetOnCollapseCommitted([&](bool) { collapseFired = true; });

    REQUIRE(f.dock.OnEvent(MousePress(1, 1))); // VCS glyph, not the active one
    REQUIRE_FALSE(f.dock.Collapsed());
    REQUIRE(f.dock.ActivePanel() == vcsId);
    REQUIRE_FALSE(collapseFired);
}

TEST_CASE("Programmatic SetCollapsed never commits; ToggleCollapsed and a rail click do", "[LeftDock]") {
    Fixture f;
    f.dock.AddPanel(U'F', "Files", f.files);

    std::vector<bool> committed;
    f.dock.SetOnCollapseCommitted([&](bool collapsed) { committed.push_back(collapsed); });

    // Programmatic changes (session restore, remembered-variable startup
    // application) must not rewrite the remembered preference.
    f.dock.SetCollapsed(true);
    f.dock.SetCollapsed(false);
    REQUIRE(committed.empty());

    f.dock.ToggleCollapsed(); // toggle-project-sidebar's path
    f.dock.ToggleCollapsed();
    REQUIRE(committed == std::vector<bool>{true, false});

    // A rail-glyph click on the active panel commits through the same helper.
    REQUIRE(f.dock.OnEvent(MousePress(1, 0)));
    REQUIRE(committed == std::vector<bool>{true, false, true});
}

TEST_CASE("Collapsing while the active content holds focus calls its OnFocusPreempted", "[LeftDock]") {
    Fixture f;
    f.dock.AddPanel(U'F', "Files", f.files);
    f.files.TakeFocus();
    REQUIRE(f.files.Focused());

    f.dock.SetCollapsed(true);
    REQUIRE(f.files.focusPreemptedCount == 1);
}

TEST_CASE("Collapsing while the active content is unfocused never calls OnFocusPreempted", "[LeftDock]") {
    Fixture f;
    f.dock.AddPanel(U'F', "Files", f.files);

    f.dock.SetCollapsed(true);
    REQUIRE(f.files.focusPreemptedCount == 0);
}

TEST_CASE("Width() reports just the rail while collapsed and the full width while expanded", "[LeftDock]") {
    Fixture f;
    f.dock.AddPanel(U'F', "Files", f.files);
    f.dock.SetWidth(25);
    REQUIRE(f.dock.Width() == 25);

    f.dock.SetCollapsed(true);
    REQUIRE(f.dock.Width() == 3);
    REQUIRE(f.dock.ExpandedWidth() == 25); // preserved, not clobbered by collapsing

    f.dock.SetCollapsed(false);
    REQUIRE(f.dock.Width() == 25);
}

TEST_CASE("Only the rail paints while collapsed -- no content border, no content Paint call", "[LeftDock]") {
    Fixture f;
    f.dock.AddPanel(U'F', "Files", f.files);
    f.dock.SetCollapsed(true);
    f.Paint();

    REQUIRE(f.files.paintCount == 0);
    // Just past the rail should still be blank background, not a border glyph.
    REQUIRE(f.screen.PixelAt(3, 0).character == " ");
}

TEST_CASE("A press on the right edge starts a resize that commits the new width on release", "[LeftDock]") {
    Fixture f;
    f.dock.AddPanel(U'F', "Files", f.files);
    f.dock.AddPanel(U'V', "VCS", f.vcs);
    f.dock.SetWidth(30);

    std::optional<int> committedWidth;
    f.dock.SetOnWidthCommitted([&](int width) { committedWidth = width; });

    REQUIRE(f.dock.OnEvent(MousePress(kWidth - 1, 5)));
    REQUIRE(f.dock.IsResizing());

    REQUIRE(f.dock.OnEvent(MouseMove(kWidth - 1 + 5, 5)));
    REQUIRE(f.dock.ExpandedWidth() == kWidth + 5); // anchored to size().width (== kWidth here), not width_

    REQUIRE(f.dock.OnEvent(MouseRelease(kWidth - 1 + 5, 5)));
    REQUIRE_FALSE(f.dock.IsResizing());
    REQUIRE(committedWidth == kWidth + 5);
}

TEST_CASE("A press-and-release with no movement never commits a width", "[LeftDock]") {
    Fixture f;
    f.dock.AddPanel(U'F', "Files", f.files);

    bool committed = false;
    f.dock.SetOnWidthCommitted([&](int) { committed = true; });

    f.dock.OnEvent(MousePress(kWidth - 1, 5));
    f.dock.OnEvent(MouseRelease(kWidth - 1, 5));
    REQUIRE_FALSE(committed);
}

TEST_CASE("Mouse events inside the content interior forward unmodified to the active panel", "[LeftDock]") {
    Fixture f;
    f.dock.AddPanel(U'F', "Files", f.files);
    f.dock.AddPanel(U'V', "VCS", f.vcs);
    f.Paint(); // establishes files.Box_() via RepositionActiveContent

    REQUIRE(f.dock.OnEvent(MousePress(10, 3)));
    REQUIRE(f.files.localEvents.size() == 1);
    // Translated to the content widget's own local coordinates -- files'
    // Box_() starts at x_min = 4, y_min = 1 (see the CommitSwitchTo test's
    // own box math), so absolute (10, 3) lands at local (6, 2).
    REQUIRE(f.files.localEvents.front().at.x == 6);
    REQUIRE(f.files.localEvents.front().at.y == 2);
}

TEST_CASE("PrepareForKeyboardFocus expands a collapsed dock without committing", "[LeftDock]") {
    Fixture f;
    f.dock.AddPanel(U'F', "Files", f.files);
    f.dock.SetCollapsed(true);

    bool collapseCommitted = false;
    f.dock.SetOnCollapseCommitted([&](bool) { collapseCommitted = true; });

    f.dock.PrepareForKeyboardFocus(&f.files);
    REQUIRE_FALSE(f.dock.Collapsed());
    REQUIRE_FALSE(collapseCommitted); // silent, ProjectSidebar::TakeKeyboardFocus's own precedent
}

TEST_CASE("NoteFocusReturned re-collapses only if PrepareForKeyboardFocus found it collapsed", "[LeftDock]") {
    Fixture f;
    f.dock.AddPanel(U'F', "Files", f.files);

    // Already expanded when focus is taken -- stays expanded afterward.
    f.dock.PrepareForKeyboardFocus(&f.files);
    f.dock.NoteFocusReturned();
    REQUIRE_FALSE(f.dock.Collapsed());

    // Collapsed when focus is taken -- goes back to collapsed afterward.
    f.dock.SetCollapsed(true);
    f.dock.PrepareForKeyboardFocus(&f.files);
    REQUIRE_FALSE(f.dock.Collapsed());
    f.dock.NoteFocusReturned();
    REQUIRE(f.dock.Collapsed());
}

TEST_CASE("NoteFocusReturned with no pending PrepareForKeyboardFocus is a safe no-op", "[LeftDock]") {
    Fixture f;
    f.dock.AddPanel(U'F', "Files", f.files);
    f.dock.NoteFocusReturned();
    REQUIRE_FALSE(f.dock.Collapsed());
}

TEST_CASE("Mouse events never forward to content while collapsed", "[LeftDock]") {
    Fixture f;
    f.dock.AddPanel(U'F', "Files", f.files);
    f.dock.SetCollapsed(true);
    f.Paint();

    // Column 10 is past the rail (kRailWidth == 3) but still inside this
    // fixture's own Box_ (collapsing doesn't itself shrink Box_ -- Width()
    // is just what a real layout pass would read next frame to size it
    // narrower). Neither the rail branch (wrong column) nor content
    // forwarding (guarded on !collapsed_) claims it.
    REQUIRE_FALSE(f.dock.OnEvent(MousePress(10, 3)));
    REQUIRE(f.files.localEvents.empty());
}
