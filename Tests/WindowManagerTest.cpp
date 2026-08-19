#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>

#include "Editor/Commands.h"
#include "Editor/Mode.h"
#include "Editor/Register.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "Text/KillRing.h"
#include "UI/ActiveBuffer.h"
#include "UI/Theme.h"
#include "UI/WindowManager.h"

namespace {

struct Fixture {
    ned::text::Buffer          buffer{"scratch"};
    ned::text::KillRing        killRing;
    ned::editor::RegisterTable registers;
    ned::text::BufferList      bufferList;

    ned::editor::CommandRegistry registry{[] {
        ned::editor::CommandRegistry r;
        ned::editor::RegisterBuiltinCommands(r);
        return r;
    }()};
    ned::editor::Keymap          globalKeymap = ned::editor::BuildDefaultGlobalKeymap();
    ned::editor::Keymap          janetKeymap; // empty -- no Janet bindings needed for these tests
    ned::ui::Theme               theme = ned::ui::DarkTheme();
    std::string                  statusMessage;

    ned::ui::WindowManager Manager() {
        return ned::ui::WindowManager(buffer, killRing, registers, bufferList, registry, janetKeymap, globalKeymap,
                                      ned::editor::FundamentalMode(), statusMessage, theme);
    }
};

// Same shape as BufferView.cpp's own MousePress-style event helpers, and the
// C-x-prefix + plain-character pattern already established in
// BufferViewTest.cpp for feeding a two-chord sequence.
void FeedSequence(ftxui::Component& root, std::initializer_list<ftxui::Event> events) {
    for (const ftxui::Event& event : events) {
        root->OnEvent(event);
    }
}

// Mirrors BufferViewTest.cpp's own RowText -- not shared, same "not worth a
// new cross-test-binary dependency for something this small" call this
// codebase's own tests already make elsewhere.
std::string RowText(ftxui::Screen& screen, int row, int width) {
    std::string out;
    for (int col = 0; col < width; ++col) {
        out += screen.PixelAt(col, row).character;
    }
    return out;
}

} // namespace

TEST_CASE("A freshly constructed WindowManager has exactly one window", "[WindowManager]") {
    Fixture                fixture;
    ned::ui::WindowManager manager = fixture.Manager();

    REQUIRE(manager.WindowCount() == 1);
}

TEST_CASE("SplitBelow creates a second window and keeps focus on the original", "[WindowManager]") {
    Fixture                fixture;
    ned::ui::WindowManager manager = fixture.Manager();

    ned::ui::ActiveBuffer* originalActiveBuffer = &manager.FocusedActiveBuffer();

    ftxui::Component root = manager.RootComponent();
    FeedSequence(root, {ftxui::Event::CtrlX, ftxui::Event::Character("2")});

    REQUIRE(manager.WindowCount() == 2);
    // Emacs' own split-window-below/-right semantics: the *new* window does
    // not steal focus -- easy to get backwards, worth its own explicit
    // assertion. Compared by ActiveBuffer identity (a real, distinct object
    // per pane), not by which buffer it currently shows -- both panes show
    // the same buffer right after a split, so buffer identity alone
    // wouldn't distinguish "same pane" from "a different pane on the same
    // content."
    REQUIRE(&manager.FocusedActiveBuffer() == originalActiveBuffer);
}

TEST_CASE("SplitRight creates a second window and keeps focus on the original", "[WindowManager]") {
    Fixture                fixture;
    ned::ui::WindowManager manager = fixture.Manager();

    ned::ui::ActiveBuffer* originalActiveBuffer = &manager.FocusedActiveBuffer();

    ftxui::Component root = manager.RootComponent();
    FeedSequence(root, {ftxui::Event::CtrlX, ftxui::Event::Character("3")});

    REQUIRE(manager.WindowCount() == 2);
    REQUIRE(&manager.FocusedActiveBuffer() == originalActiveBuffer);
}

TEST_CASE("Recursive splits produce three windows", "[WindowManager]") {
    Fixture                fixture;
    ned::ui::WindowManager manager = fixture.Manager();

    ftxui::Component root = manager.RootComponent();
    FeedSequence(root, {ftxui::Event::CtrlX, ftxui::Event::Character("2")}); // 2 windows
    FeedSequence(root, {ftxui::Event::CtrlX, ftxui::Event::Character("3")}); // split the (still-focused) first again -> 3

    REQUIRE(manager.WindowCount() == 3);
}

TEST_CASE("Splitting a pane that isn't first in tree order doesn't drop the new pane", "[WindowManager]") {
    // Regression test for a real, coredump-confirmed crash: SplitLeafInTree
    // used to take its new-pane argument by value and std::move it into both
    // recursive branches unconditionally, silently destroying the new Pane
    // the moment the search had to pass over any non-matching sibling first
    // -- leaving a Leaf node with a null Pane spliced into the live tree,
    // which crashed the very next RebuildComponentTree() call the instant
    // BuildComponent tried node->pane->Component() on it. "Recursive splits
    // produce three windows" above never exercised this, since it always
    // splits the still-focused *first* leaf; focusing the *second* pane
    // before splitting again is what actually reproduces it.
    Fixture                fixture;
    ned::ui::WindowManager manager = fixture.Manager();

    ftxui::Component root = manager.RootComponent();
    FeedSequence(root, {ftxui::Event::CtrlX, ftxui::Event::Character("2")}); // 2 panes
    FeedSequence(root, {ftxui::Event::CtrlX, ftxui::Event::Character("o")}); // focus the *second* (new) pane
    FeedSequence(root, {ftxui::Event::CtrlX, ftxui::Event::Character("3")}); // split it -> 3 panes

    REQUIRE(manager.WindowCount() == 3);

    ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(80), ftxui::Dimension::Fixed(24));
    ftxui::Render(screen, root->Render()); // would crash (null Pane::Component()) before the fix
    SUCCEED();
}

TEST_CASE("other-window cycles focus between windows", "[WindowManager]") {
    Fixture                fixture;
    ned::ui::WindowManager manager = fixture.Manager();

    ned::ui::ActiveBuffer* originalActiveBuffer = &manager.FocusedActiveBuffer();

    ftxui::Component root = manager.RootComponent();
    FeedSequence(root, {ftxui::Event::CtrlX, ftxui::Event::Character("2")});
    REQUIRE(&manager.FocusedActiveBuffer() == originalActiveBuffer);

    FeedSequence(root, {ftxui::Event::CtrlX, ftxui::Event::Character("o")});
    REQUIRE(&manager.FocusedActiveBuffer() != originalActiveBuffer); // focus moved to the new pane

    FeedSequence(root, {ftxui::Event::CtrlX, ftxui::Event::Character("o")});
    REQUIRE(&manager.FocusedActiveBuffer() == originalActiveBuffer); // and back again -- only 2 windows to cycle
}

TEST_CASE("delete-window on the focused pane in a 2-window split focuses the survivor", "[WindowManager]") {
    Fixture                fixture;
    ned::ui::WindowManager manager = fixture.Manager();

    ftxui::Component root = manager.RootComponent();
    FeedSequence(root, {ftxui::Event::CtrlX, ftxui::Event::Character("2")});
    REQUIRE(manager.WindowCount() == 2);

    ned::ui::ActiveBuffer* survivorActiveBuffer = &manager.FocusedActiveBuffer();
    FeedSequence(root, {ftxui::Event::CtrlX, ftxui::Event::Character("o")}); // move focus to the new pane
    REQUIRE(&manager.FocusedActiveBuffer() != survivorActiveBuffer);

    // Delete whichever pane is now focused (the *new* one) -- the original
    // survives and should regain focus.
    FeedSequence(root, {ftxui::Event::CtrlX, ftxui::Event::Character("0")});

    REQUIRE(manager.WindowCount() == 1);
    REQUIRE(&manager.FocusedActiveBuffer() == survivorActiveBuffer);
}

TEST_CASE("delete-window on the sole window is a no-op reporting via statusMessage", "[WindowManager]") {
    Fixture                fixture;
    ned::ui::WindowManager manager = fixture.Manager();

    ftxui::Component root = manager.RootComponent();
    FeedSequence(root, {ftxui::Event::CtrlX, ftxui::Event::Character("0")});

    REQUIRE(manager.WindowCount() == 1);
    REQUIRE_FALSE(fixture.statusMessage.empty());
}

TEST_CASE("delete-other-windows collapses back to a single window", "[WindowManager]") {
    Fixture                fixture;
    ned::ui::WindowManager manager = fixture.Manager();

    ftxui::Component root = manager.RootComponent();
    FeedSequence(root, {ftxui::Event::CtrlX, ftxui::Event::Character("2")});
    FeedSequence(root, {ftxui::Event::CtrlX, ftxui::Event::Character("3")});
    REQUIRE(manager.WindowCount() == 3);

    ned::ui::ActiveBuffer* focusedBeforeCollapse = &manager.FocusedActiveBuffer();
    FeedSequence(root, {ftxui::Event::CtrlX, ftxui::Event::Character("1")});

    REQUIRE(manager.WindowCount() == 1);
    REQUIRE(&manager.FocusedActiveBuffer() == focusedBeforeCollapse); // the focused one is the survivor
}

TEST_CASE("delete-other-windows on the sole window is a safe no-op", "[WindowManager]") {
    Fixture                fixture;
    ned::ui::WindowManager manager = fixture.Manager();

    ftxui::Component root = manager.RootComponent();
    FeedSequence(root, {ftxui::Event::CtrlX, ftxui::Event::Character("1")});

    REQUIRE(manager.WindowCount() == 1);
}

TEST_CASE("Closing a buffer shown in a different, unfocused pane retargets that pane, not the focused one",
          "[WindowManager]") {
    Fixture                fixture;
    ned::text::Buffer&     other   = fixture.bufferList.CreateBuffer("other");
    ned::ui::WindowManager manager = fixture.Manager();

    ftxui::Component root = manager.RootComponent();
    FeedSequence(root, {ftxui::Event::CtrlX, ftxui::Event::Character("2")}); // 2 panes, both on "scratch"

    FeedSequence(root, {ftxui::Event::CtrlX, ftxui::Event::Character("o")}); // focus the new pane
    manager.FocusedActiveBuffer().Set(other);                                // that pane now shows "other"
    REQUIRE(&manager.FocusedActiveBuffer().Get() == &other);

    FeedSequence(root, {ftxui::Event::CtrlX, ftxui::Event::Character("o")}); // back to the original pane
    REQUIRE(&manager.FocusedActiveBuffer().Get() != &other);

    manager.RequestCloseBuffer(other); // closes "other", currently shown only in the *unfocused* pane

    REQUIRE(fixture.bufferList.Find("other") == nullptr);

    FeedSequence(root, {ftxui::Event::CtrlX, ftxui::Event::Character("o")}); // check the other pane's own state
    REQUIRE(&manager.FocusedActiveBuffer().Get() != &other);                 // retargeted, not left dangling
}

TEST_CASE("NotifyBufferClosing retargets every pane showing the closing buffer, including the focused one",
          "[WindowManager]") {
    // Regression test for a real, coredump-confirmed crash: ProjectSidebar::
    // OpenFileEntry closes the outgoing single-click-preview buffer directly
    // (bufferList_.Close(...)), with no pane-side reassignment of its own the
    // way BufferView::CloseBufferNow has -- so, unlike HandleBufferClosed
    // (see the test above), *no* pane can be skipped here, including
    // whichever one is currently focused.
    Fixture                fixture;
    ned::text::Buffer&     other   = fixture.bufferList.CreateBuffer("other");
    ned::ui::WindowManager manager = fixture.Manager();

    ftxui::Component root = manager.RootComponent();
    FeedSequence(root, {ftxui::Event::CtrlX, ftxui::Event::Character("2")}); // 2 panes

    // Point both panes at "other" -- simulating two panes both showing the
    // same single-click-preview buffer.
    manager.FocusedActiveBuffer().Set(other);
    FeedSequence(root, {ftxui::Event::CtrlX, ftxui::Event::Character("o")});
    manager.FocusedActiveBuffer().Set(other);

    manager.NotifyBufferClosing(other); // must run before the buffer is actually freed
    fixture.bufferList.Close("other");

    REQUIRE(&manager.FocusedActiveBuffer().Get() != &other); // this (currently focused) pane retargeted
    FeedSequence(root, {ftxui::Event::CtrlX, ftxui::Event::Character("o")});
    REQUIRE(&manager.FocusedActiveBuffer().Get() != &other); // the other pane retargeted too
}

TEST_CASE("RootComponent renders both panes after a split without crashing", "[WindowManager]") {
    Fixture                fixture;
    ned::ui::WindowManager manager = fixture.Manager();

    ftxui::Component root = manager.RootComponent();
    FeedSequence(root, {ftxui::Event::CtrlX, ftxui::Event::Character("2")});

    ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(80), ftxui::Dimension::Fixed(24));
    ftxui::Render(screen, root->Render());

    // No specific content assertion -- this is a headless "does the whole
    // rebuilt Component tree paint at all" smoke test, the same role
    // TabBarTest.cpp's/ProjectSidebarTest.cpp's own Screen+Canvas
    // construction plays for a single widget, just exercised through the
    // real Container/Renderer tree WindowManager actually builds.
    SUCCEED();
}

TEST_CASE("split-window-below is a safe no-op reachable through the real Dispatcher chain", "[WindowManager]") {
    // Not really a no-op assertion -- this is the "does the whole
    // command -> InteractiveRequest -> BufferView -> WindowManager chain
    // actually work end to end through a real focused BufferView's own
    // Dispatcher" check, covered implicitly by every test above (all of
    // them drive C-x 2/3/0/1/o through FeedSequence -> RootComponent()
    // ->OnEvent, which is exactly this whole chain). This case exists to
    // name that coverage explicitly and pin the exact keybindings.
    Fixture                fixture;
    ned::ui::WindowManager manager = fixture.Manager();
    ftxui::Component       root    = manager.RootComponent();

    FeedSequence(root, {ftxui::Event::CtrlX, ftxui::Event::Character("2")});
    REQUIRE(manager.WindowCount() == 2);
}

TEST_CASE("Switching a pane's active buffer resolves a fresh Mode for the new buffer, per-buffer-mode follow-up",
          "[WindowManager]") {
    Fixture                fixture;
    ned::ui::WindowManager manager = fixture.Manager();

    // fixture.buffer has no path, so ModeForBuffer resolves it to
    // fundamental-mode -- ModeLine renders "(fundamental-mode)" somewhere on
    // its row, giving a real, end-to-end (not just Pane::ModeRef()-by-name)
    // proof that the whole chain -- BufferView::Paint -> onActiveBufferChanged_
    // -> Pane::mode_ reassignment -> the same Mode& ModeLine already
    // references -- actually rendered differently, not merely that the Mode
    // object's own .name field changed.
    ftxui::Component root   = manager.RootComponent();
    ftxui::Screen     screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(80), ftxui::Dimension::Fixed(24));
    ftxui::Render(screen, root->Render());

    bool foundFundamental = false;
    for (int row = 0; row < 24; ++row) {
        if (RowText(screen, row, 80).find("(fundamental-mode)") != std::string::npos) {
            foundFundamental = true;
        }
    }
    REQUIRE(foundFundamental);

    // A not-yet-existing .json path is enough -- OpenOrCreateFile creates it
    // via Buffer::NewFile with no disk I/O, and ModeForBuffer only ever
    // looks at the buffer's own associated path, never its content.
    const std::filesystem::path jsonPath =
        std::filesystem::temp_directory_path() / "ned_window_manager_test_mode_switch.json";
    ned::text::Buffer& jsonBuffer = fixture.bufferList.OpenOrCreateFile(jsonPath);
    manager.FocusedActiveBuffer().Set(jsonBuffer);

    screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(80), ftxui::Dimension::Fixed(24));
    ftxui::Render(screen, root->Render());

    bool foundJson = false;
    for (int row = 0; row < 24; ++row) {
        if (RowText(screen, row, 80).find("(json-mode)") != std::string::npos) {
            foundJson = true;
        }
    }
    REQUIRE(foundJson);
}
