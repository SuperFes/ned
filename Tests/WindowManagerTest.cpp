#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

#include "Editor/Commands.h"
#include "Editor/MinimapSettings.h"
#include "Editor/Mode.h"
#include "Editor/Register.h"
#include "TestEvents.h"
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

    // Returned as a direct prvalue (not a named local) -- WindowManager's
    // own copy constructor is deleted and it has no move constructor either
    // (Container/reference-member fallout), so this relies on C++17's
    // guaranteed copy elision for a `return T(...)` matching the function's
    // own return type exactly; a named local would need an actual move and
    // fail to compile. Callers must call TakeFocus() themselves afterward
    // -- see FeedSequence's own header comment for why.
    ned::ui::WindowManager Manager() {
        return ned::ui::WindowManager(buffer, killRing, registers, bufferList, registry, janetKeymap, globalKeymap,
                                      ned::editor::FundamentalMode(), statusMessage, theme);
    }
};

// Same shape as BufferView.cpp's own MousePress-style event helpers, and the
// C-x-prefix + plain-character pattern already established in
// BufferViewTest.cpp for feeding a two-chord sequence. `root` is unused for
// keyboard events -- matches main.cpp's own real dispatch exactly (see its
// own EventLoopCallbacks::onEvent comment): a keyboard Event goes straight
// to FocusedWidget(), never broadcast through the Container tree the way a
// mouse Event is, since every pane's BufferView would otherwise react to
// the same keystroke regardless of which one is actually focused (unlike
// FTXUI, which limited keyboard delivery to the focus-chain branch itself;
// see Widget.h's own header comment on why Notcurses' flat focus registry
// replaces that mechanism directly instead of replicating it in Container).
void FeedSequence(ned::ui::Widget& /*root*/, std::initializer_list<ned::ui::Event> events) {
    for (const ned::ui::Event& event : events) {
        if (ned::ui::Widget* focused = ned::ui::FocusedWidget()) {
            focused->OnEvent(event);
        }
    }
}

// Mirrors BufferViewTest.cpp's own RowText -- not shared, same "not worth a
// new cross-test-binary dependency for something this small" call this
// codebase's own tests already make elsewhere.
std::string RowText(ned::ui::Screen& screen, int row, int width) {
    std::string out;
    for (int col = 0; col < width; ++col) {
        out += screen.PixelAt(col, row).character;
    }
    return out;
}

// FTXUI -> Notcurses migration: was ftxui::Render(screen, root->Render())
// -- Layout.h's own Container needs its Box_() actually set before Paint()
// can lay out its children at all (production code does this via
// EventLoop's own onResize callback; nothing here ever calls that), so this
// helper does both steps together: size a fresh Screen to widthxheight,
// give root that same rectangle as its Box_(), and paint.
void RenderFullScreen(ned::ui::Widget& root, ned::ui::Screen& screen) {
    root.SetBox_(ned::ui::Box{.x_min = 0, .x_max = screen.Width() - 1, .y_min = 0, .y_max = screen.Height() - 1});
    root.Paint(ned::ui::Canvas(screen, root.Box_()));
}

} // namespace

TEST_CASE("A freshly constructed WindowManager has exactly one window", "[WindowManager]") {
    Fixture                fixture;
    ned::ui::WindowManager manager = fixture.Manager();
    manager.TakeFocus(); // FTXUI -> Notcurses migration: see Fixture::Manager()'s own doc comment

    REQUIRE(manager.WindowCount() == 1);
}

TEST_CASE("SplitBelow creates a second window and keeps focus on the original", "[WindowManager]") {
    Fixture                fixture;
    ned::ui::WindowManager manager = fixture.Manager();
    manager.TakeFocus(); // FTXUI -> Notcurses migration: see Fixture::Manager()'s own doc comment

    ned::ui::ActiveBuffer* originalActiveBuffer = &manager.FocusedActiveBuffer();

    ned::ui::Widget& root = manager.RootComponent();
    FeedSequence(root, {ned::ui::test::Ctrl('x'), ned::ui::test::Character("2")});

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
    manager.TakeFocus(); // FTXUI -> Notcurses migration: see Fixture::Manager()'s own doc comment

    ned::ui::ActiveBuffer* originalActiveBuffer = &manager.FocusedActiveBuffer();

    ned::ui::Widget& root = manager.RootComponent();
    FeedSequence(root, {ned::ui::test::Ctrl('x'), ned::ui::test::Character("3")});

    REQUIRE(manager.WindowCount() == 2);
    REQUIRE(&manager.FocusedActiveBuffer() == originalActiveBuffer);
}

TEST_CASE("Recursive splits produce three windows", "[WindowManager]") {
    Fixture                fixture;
    ned::ui::WindowManager manager = fixture.Manager();
    manager.TakeFocus(); // FTXUI -> Notcurses migration: see Fixture::Manager()'s own doc comment

    ned::ui::Widget& root = manager.RootComponent();
    FeedSequence(root, {ned::ui::test::Ctrl('x'), ned::ui::test::Character("2")}); // 2 windows
    FeedSequence(root, {ned::ui::test::Ctrl('x'), ned::ui::test::Character("3")}); // split the (still-focused) first again -> 3

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
    manager.TakeFocus(); // FTXUI -> Notcurses migration: see Fixture::Manager()'s own doc comment

    ned::ui::Widget& root = manager.RootComponent();
    FeedSequence(root, {ned::ui::test::Ctrl('x'), ned::ui::test::Character("2")}); // 2 panes
    FeedSequence(root, {ned::ui::test::Ctrl('x'), ned::ui::test::Character("o")}); // focus the *second* (new) pane
    FeedSequence(root, {ned::ui::test::Ctrl('x'), ned::ui::test::Character("3")}); // split it -> 3 panes

    REQUIRE(manager.WindowCount() == 3);

    ned::ui::Screen screen = ned::ui::Screen(80, 24);
    RenderFullScreen(root, screen); // would crash (null Pane::Component()) before the fix
    SUCCEED();
}

TEST_CASE("other-window cycles focus between windows", "[WindowManager]") {
    Fixture                fixture;
    ned::ui::WindowManager manager = fixture.Manager();
    manager.TakeFocus(); // FTXUI -> Notcurses migration: see Fixture::Manager()'s own doc comment

    ned::ui::ActiveBuffer* originalActiveBuffer = &manager.FocusedActiveBuffer();

    ned::ui::Widget& root = manager.RootComponent();
    FeedSequence(root, {ned::ui::test::Ctrl('x'), ned::ui::test::Character("2")});
    REQUIRE(&manager.FocusedActiveBuffer() == originalActiveBuffer);

    FeedSequence(root, {ned::ui::test::Ctrl('x'), ned::ui::test::Character("o")});
    REQUIRE(&manager.FocusedActiveBuffer() != originalActiveBuffer); // focus moved to the new pane

    FeedSequence(root, {ned::ui::test::Ctrl('x'), ned::ui::test::Character("o")});
    REQUIRE(&manager.FocusedActiveBuffer() == originalActiveBuffer); // and back again -- only 2 windows to cycle
}

TEST_CASE("delete-window on the focused pane in a 2-window split focuses the survivor", "[WindowManager]") {
    Fixture                fixture;
    ned::ui::WindowManager manager = fixture.Manager();
    manager.TakeFocus(); // FTXUI -> Notcurses migration: see Fixture::Manager()'s own doc comment

    ned::ui::Widget& root = manager.RootComponent();
    FeedSequence(root, {ned::ui::test::Ctrl('x'), ned::ui::test::Character("2")});
    REQUIRE(manager.WindowCount() == 2);

    ned::ui::ActiveBuffer* survivorActiveBuffer = &manager.FocusedActiveBuffer();
    FeedSequence(root, {ned::ui::test::Ctrl('x'), ned::ui::test::Character("o")}); // move focus to the new pane
    REQUIRE(&manager.FocusedActiveBuffer() != survivorActiveBuffer);

    // Delete whichever pane is now focused (the *new* one) -- the original
    // survives and should regain focus.
    FeedSequence(root, {ned::ui::test::Ctrl('x'), ned::ui::test::Character("0")});

    REQUIRE(manager.WindowCount() == 1);
    REQUIRE(&manager.FocusedActiveBuffer() == survivorActiveBuffer);
}

TEST_CASE("delete-window on the sole window is a no-op reporting via statusMessage", "[WindowManager]") {
    Fixture                fixture;
    ned::ui::WindowManager manager = fixture.Manager();
    manager.TakeFocus(); // FTXUI -> Notcurses migration: see Fixture::Manager()'s own doc comment

    ned::ui::Widget& root = manager.RootComponent();
    FeedSequence(root, {ned::ui::test::Ctrl('x'), ned::ui::test::Character("0")});

    REQUIRE(manager.WindowCount() == 1);
    REQUIRE_FALSE(fixture.statusMessage.empty());
}

TEST_CASE("delete-other-windows collapses back to a single window", "[WindowManager]") {
    Fixture                fixture;
    ned::ui::WindowManager manager = fixture.Manager();
    manager.TakeFocus(); // FTXUI -> Notcurses migration: see Fixture::Manager()'s own doc comment

    ned::ui::Widget& root = manager.RootComponent();
    FeedSequence(root, {ned::ui::test::Ctrl('x'), ned::ui::test::Character("2")});
    FeedSequence(root, {ned::ui::test::Ctrl('x'), ned::ui::test::Character("3")});
    REQUIRE(manager.WindowCount() == 3);

    ned::ui::ActiveBuffer* focusedBeforeCollapse = &manager.FocusedActiveBuffer();
    FeedSequence(root, {ned::ui::test::Ctrl('x'), ned::ui::test::Character("1")});

    REQUIRE(manager.WindowCount() == 1);
    REQUIRE(&manager.FocusedActiveBuffer() == focusedBeforeCollapse); // the focused one is the survivor
}

TEST_CASE("delete-other-windows on the sole window is a safe no-op", "[WindowManager]") {
    Fixture                fixture;
    ned::ui::WindowManager manager = fixture.Manager();
    manager.TakeFocus(); // FTXUI -> Notcurses migration: see Fixture::Manager()'s own doc comment

    ned::ui::Widget& root = manager.RootComponent();
    FeedSequence(root, {ned::ui::test::Ctrl('x'), ned::ui::test::Character("1")});

    REQUIRE(manager.WindowCount() == 1);
}

TEST_CASE("Closing a buffer shown in a different, unfocused pane retargets that pane, not the focused one",
          "[WindowManager]") {
    Fixture                fixture;
    ned::text::Buffer&     other   = fixture.bufferList.CreateBuffer("other");
    ned::ui::WindowManager manager = fixture.Manager();
    manager.TakeFocus(); // FTXUI -> Notcurses migration: see Fixture::Manager()'s own doc comment

    ned::ui::Widget& root = manager.RootComponent();
    FeedSequence(root, {ned::ui::test::Ctrl('x'), ned::ui::test::Character("2")}); // 2 panes, both on "scratch"

    FeedSequence(root, {ned::ui::test::Ctrl('x'), ned::ui::test::Character("o")}); // focus the new pane
    manager.FocusedActiveBuffer().Set(other);                                      // that pane now shows "other"
    REQUIRE(&manager.FocusedActiveBuffer().Get() == &other);

    FeedSequence(root, {ned::ui::test::Ctrl('x'), ned::ui::test::Character("o")}); // back to the original pane
    REQUIRE(&manager.FocusedActiveBuffer().Get() != &other);

    manager.RequestCloseBuffer(other); // closes "other", currently shown only in the *unfocused* pane

    REQUIRE(fixture.bufferList.Find("other") == nullptr);

    FeedSequence(root, {ned::ui::test::Ctrl('x'), ned::ui::test::Character("o")}); // check the other pane's own state
    REQUIRE(&manager.FocusedActiveBuffer().Get() != &other);                       // retargeted, not left dangling
}

TEST_CASE("Closing a buffer shown in an unfocused pane retargets it onto the most-recently-used buffer, not list order",
          "[WindowManager]") {
    Fixture                fixture;
    ned::text::Buffer&     a       = fixture.bufferList.CreateBuffer("a");
    ned::text::Buffer&     b       = fixture.bufferList.CreateBuffer("b");
    ned::text::Buffer&     closing = fixture.bufferList.CreateBuffer("closing");
    ned::ui::WindowManager manager = fixture.Manager();
    manager.TakeFocus(); // FTXUI -> Notcurses migration: see Fixture::Manager()'s own doc comment

    ned::ui::Widget& root = manager.RootComponent();
    FeedSequence(root, {ned::ui::test::Ctrl('x'), ned::ui::test::Character("2")}); // 2 panes, both on "scratch"

    // Touch "a" then "b" in the focused pane -- bufferList_'s own creation
    // (list) order is [a, b, closing], but MRU order is now [a, b] (b most
    // recent), the opposite of list order for these two.
    manager.FocusedActiveBuffer().Set(a);
    manager.FocusedActiveBuffer().Set(b);

    FeedSequence(root, {ned::ui::test::Ctrl('x'), ned::ui::test::Character("o")}); // the other, unfocused pane
    manager.FocusedActiveBuffer().Set(closing);
    FeedSequence(root, {ned::ui::test::Ctrl('x'), ned::ui::test::Character("o")}); // back to the focused pane (now on "b")

    manager.RequestCloseBuffer(closing); // closes "closing", shown only in the unfocused pane

    FeedSequence(root, {ned::ui::test::Ctrl('x'), ned::ui::test::Character("o")}); // check the retargeted pane
    REQUIRE(&manager.FocusedActiveBuffer().Get() == &b); // MRU pick -- not "a", list order's first non-closing entry
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
    manager.TakeFocus(); // FTXUI -> Notcurses migration: see Fixture::Manager()'s own doc comment

    ned::ui::Widget& root = manager.RootComponent();
    FeedSequence(root, {ned::ui::test::Ctrl('x'), ned::ui::test::Character("2")}); // 2 panes

    // Point both panes at "other" -- simulating two panes both showing the
    // same single-click-preview buffer.
    manager.FocusedActiveBuffer().Set(other);
    FeedSequence(root, {ned::ui::test::Ctrl('x'), ned::ui::test::Character("o")});
    manager.FocusedActiveBuffer().Set(other);

    manager.NotifyBufferClosing(other); // must run before the buffer is actually freed
    fixture.bufferList.Close("other");

    REQUIRE(&manager.FocusedActiveBuffer().Get() != &other); // this (currently focused) pane retargeted
    FeedSequence(root, {ned::ui::test::Ctrl('x'), ned::ui::test::Character("o")});
    REQUIRE(&manager.FocusedActiveBuffer().Get() != &other); // the other pane retargeted too
}

TEST_CASE("RootComponent renders both panes after a split without crashing", "[WindowManager]") {
    Fixture                fixture;
    ned::ui::WindowManager manager = fixture.Manager();
    manager.TakeFocus(); // FTXUI -> Notcurses migration: see Fixture::Manager()'s own doc comment

    ned::ui::Widget& root = manager.RootComponent();
    FeedSequence(root, {ned::ui::test::Ctrl('x'), ned::ui::test::Character("2")});

    ned::ui::Screen screen = ned::ui::Screen(80, 24);
    RenderFullScreen(root, screen);

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
    manager.TakeFocus(); // FTXUI -> Notcurses migration: see Fixture::Manager()'s own doc comment
    ned::ui::Widget& root = manager.RootComponent();

    FeedSequence(root, {ned::ui::test::Ctrl('x'), ned::ui::test::Character("2")});
    REQUIRE(manager.WindowCount() == 2);
}

TEST_CASE("Switching a pane's active buffer resolves a fresh Mode for the new buffer, per-buffer-mode follow-up",
          "[WindowManager]") {
    Fixture                fixture;
    ned::ui::WindowManager manager = fixture.Manager();
    manager.TakeFocus(); // FTXUI -> Notcurses migration: see Fixture::Manager()'s own doc comment

    // fixture.buffer has no path, so ModeForBuffer resolves it to
    // fundamental-mode -- ModeLine renders "(fundamental-mode)" somewhere on
    // its row, giving a real, end-to-end (not just Pane::ModeRef()-by-name)
    // proof that the whole chain -- BufferView::Paint -> onActiveBufferChanged_
    // -> Pane::mode_ reassignment -> the same Mode& ModeLine already
    // references -- actually rendered differently, not merely that the Mode
    // object's own .name field changed.
    ned::ui::Widget& root   = manager.RootComponent();
    ned::ui::Screen  screen = ned::ui::Screen(80, 24);
    RenderFullScreen(root, screen);

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

    screen = ned::ui::Screen(80, 24);
    RenderFullScreen(root, screen);

    bool foundJson = false;
    for (int row = 0; row < 24; ++row) {
        if (RowText(screen, row, 80).find("(json-mode)") != std::string::npos) {
            foundJson = true;
        }
    }
    REQUIRE(foundJson);
}

namespace {
// Process-wide state -- mirrors MinimapTest.cpp's own MinimapSettingsGuard.
struct MinimapSettingsGuard {
    ~MinimapSettingsGuard() {
        ned::editor::SetMinimapEnabled(true);
    }
};
} // namespace

TEST_CASE("A freshly constructed Pane seeds Minimap/ScrollColumn as exact opposites of editor::MinimapEnabled()",
          "[WindowManager]") {
    const MinimapSettingsGuard guard;

    ned::editor::SetMinimapEnabled(true);
    {
        Fixture                fixture;
        ned::ui::WindowManager manager = fixture.Manager();
        manager.TakeFocus();
        REQUIRE(manager.FocusedPaneMinimapActive());
        REQUIRE_FALSE(manager.FocusedPaneScrollColumnActive());
    }

    ned::editor::SetMinimapEnabled(false);
    {
        Fixture                fixture;
        ned::ui::WindowManager manager = fixture.Manager();
        manager.TakeFocus();
        REQUIRE_FALSE(manager.FocusedPaneMinimapActive());
        REQUIRE(manager.FocusedPaneScrollColumnActive());
    }
}

TEST_CASE("toggle-minimap flips Minimap/ScrollColumn active flags in lockstep opposition", "[WindowManager]") {
    const MinimapSettingsGuard guard;
    ned::editor::SetMinimapEnabled(true);

    Fixture                fixture;
    ned::ui::WindowManager manager = fixture.Manager();
    manager.TakeFocus(); // FTXUI -> Notcurses migration: see Fixture::Manager()'s own doc comment

    ned::ui::Widget& root = manager.RootComponent();

    // Never both active, never both inactive -- checked at every step, not
    // just the final state, since a bug that briefly desyncs the two would
    // otherwise only show up as a layout glitch, not a test failure.
    REQUIRE(manager.FocusedPaneMinimapActive());
    REQUIRE_FALSE(manager.FocusedPaneScrollColumnActive());

    FeedSequence(root, {ned::ui::test::Ctrl('c'), ned::ui::test::Character("m")});
    REQUIRE_FALSE(manager.FocusedPaneMinimapActive());
    REQUIRE(manager.FocusedPaneScrollColumnActive());

    FeedSequence(root, {ned::ui::test::Ctrl('c'), ned::ui::test::Character("m")});
    REQUIRE(manager.FocusedPaneMinimapActive());
    REQUIRE_FALSE(manager.FocusedPaneScrollColumnActive());
}

TEST_CASE("SetOnTerminalToggle reaches the focused pane and survives a split", "[WindowManager]") {
    Fixture                fixture;
    ned::ui::WindowManager manager = fixture.Manager();
    manager.TakeFocus();

    ned::ui::Widget& root = manager.RootComponent();

    int toggles = 0;
    manager.SetOnTerminalToggle([&toggles] { ++toggles; });

    FeedSequence(root, {ned::ui::test::Ctrl('c'), ned::ui::test::Character('t')});
    REQUIRE(toggles == 1);

    // A pane created *after* registration inherits the handler (MakePane
    // applies the stored callback, the SetThemeApplier convention).
    FeedSequence(root, {ned::ui::test::Ctrl('x'), ned::ui::test::Character("2")});
    FeedSequence(root, {ned::ui::test::Ctrl('x'), ned::ui::test::Character("o")}); // focus the new pane
    FeedSequence(root, {ned::ui::test::Ctrl('c'), ned::ui::test::Character('t')});
    REQUIRE(toggles == 2);
}
