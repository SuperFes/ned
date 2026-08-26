#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>

#include "Editor/Commands.h"
#include "Editor/MinimapSettings.h"
#include "Editor/Mode.h"
#include "Editor/ProjectRoot.h"
#include "Editor/ProjectSession.h"
#include "Editor/PromptHistory.h"
#include "Editor/Register.h"
#include "Editor/Vcs/VcsProvider.h"
#include "Editor/Vcs/VcsProviderRegistry.h"
#include "Editor/Vcs/VcsRunner.h"
#include "TestEvents.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "Text/KillRing.h"
#include "UI/ActiveBuffer.h"
#include "UI/EventLoop.h"
#include "UI/Theme.h"
#include "UI/WindowManager.h"

namespace {

struct Fixture {
    ned::text::Buffer          buffer{"scratch"};
    ned::text::KillRing        killRing;
    ned::editor::RegisterTable registers;
    ned::editor::PromptHistory promptHistory;
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
        return ned::ui::WindowManager(buffer, killRing, registers, promptHistory, bufferList, registry, janetKeymap,
                                      globalKeymap, ned::editor::FundamentalMode(), statusMessage, theme);
    }
};

// Same shape as BufferView.cpp's own MousePress-style event helpers, and the
// C-x-prefix + plain-character pattern already established in
// BufferViewTest.cpp for feeding a two-chord sequence. `root` is unused for
// keyboard events -- matches main.cpp's own real dispatch exactly (see its
// own EventLoopCallbacks::onEvent comment): a keyboard Event goes straight
// to FocusedWidget(), never broadcast through the Container tree the way a
// mouse Event is, since every pane's BufferView would otherwise react to
// the same keystroke regardless of which one is actually focused (see
// Widget.h's own header comment on Notcurses' flat focus registry).
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

// Layout.h's own Container needs its Box_() actually set before Paint() can
// lay out its children at all (production code does this via EventLoop's own
// onResize callback; nothing here ever calls that), so this helper does both
// steps together: size a fresh Screen to widthxheight, give root that same
// rectangle as its Box_(), and paint.
void RenderFullScreen(ned::ui::Widget& root, ned::ui::Screen& screen) {
    root.SetBox_(ned::ui::Box{.x_min = 0, .x_max = screen.Width() - 1, .y_min = 0, .y_max = screen.Height() - 1});
    root.Paint(ned::ui::Canvas(screen, root.Box_()));
}

// vcs-diff-gutter-staleness follow-up: mirrors BufferViewDiffGutterTest.cpp's
// own RecordingProvider (a distinct anonymous-namespace class -- can't share
// across translation units), counting instead of just recording so a
// multi-pane test can confirm every pane, not just one, requested a diff.
// Detect below is VcsProvider's only pure-virtual method; every other
// operation not overridden here already default-throws "not supported by
// this provider," which is fine -- nothing but DiffArgv is ever called.
class CountingDiffProvider : public ned::editor::vcs::VcsProvider {
  public:
    explicit CountingDiffProvider(int& count) : count_(count) {
    }

    [[nodiscard]] bool Detect(const std::filesystem::path&) const override {
        return true;
    }
    [[nodiscard]] ned::editor::vcs::VcsCommandSpec DiffArgv(const std::filesystem::path&) const override {
        ++count_;
        throw std::runtime_error("recorded -- no real spawn wanted");
    }

  private:
    int& count_;
};

} // namespace

TEST_CASE("A freshly constructed WindowManager has exactly one window", "[WindowManager]") {
    Fixture                fixture;
    ned::ui::WindowManager manager = fixture.Manager();
    manager.TakeFocus(); // see Fixture::Manager()'s own doc comment

    REQUIRE(manager.WindowCount() == 1);
}

TEST_CASE("SplitBelow creates a second window and keeps focus on the original", "[WindowManager]") {
    Fixture                fixture;
    ned::ui::WindowManager manager = fixture.Manager();
    manager.TakeFocus(); // see Fixture::Manager()'s own doc comment

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
    manager.TakeFocus(); // see Fixture::Manager()'s own doc comment

    ned::ui::ActiveBuffer* originalActiveBuffer = &manager.FocusedActiveBuffer();

    ned::ui::Widget& root = manager.RootComponent();
    FeedSequence(root, {ned::ui::test::Ctrl('x'), ned::ui::test::Character("3")});

    REQUIRE(manager.WindowCount() == 2);
    REQUIRE(&manager.FocusedActiveBuffer() == originalActiveBuffer);
}

TEST_CASE("Recursive splits produce three windows", "[WindowManager]") {
    Fixture                fixture;
    ned::ui::WindowManager manager = fixture.Manager();
    manager.TakeFocus(); // see Fixture::Manager()'s own doc comment

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
    manager.TakeFocus(); // see Fixture::Manager()'s own doc comment

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
    manager.TakeFocus(); // see Fixture::Manager()'s own doc comment

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
    manager.TakeFocus(); // see Fixture::Manager()'s own doc comment

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
    manager.TakeFocus(); // see Fixture::Manager()'s own doc comment

    ned::ui::Widget& root = manager.RootComponent();
    FeedSequence(root, {ned::ui::test::Ctrl('x'), ned::ui::test::Character("0")});

    REQUIRE(manager.WindowCount() == 1);
    REQUIRE_FALSE(fixture.statusMessage.empty());
}

TEST_CASE("delete-other-windows collapses back to a single window", "[WindowManager]") {
    Fixture                fixture;
    ned::ui::WindowManager manager = fixture.Manager();
    manager.TakeFocus(); // see Fixture::Manager()'s own doc comment

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
    manager.TakeFocus(); // see Fixture::Manager()'s own doc comment

    ned::ui::Widget& root = manager.RootComponent();
    FeedSequence(root, {ned::ui::test::Ctrl('x'), ned::ui::test::Character("1")});

    REQUIRE(manager.WindowCount() == 1);
}

TEST_CASE("Closing a buffer shown in a different, unfocused pane retargets that pane, not the focused one",
          "[WindowManager]") {
    Fixture                fixture;
    ned::text::Buffer&     other   = fixture.bufferList.CreateBuffer("other");
    ned::ui::WindowManager manager = fixture.Manager();
    manager.TakeFocus(); // see Fixture::Manager()'s own doc comment

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
    manager.TakeFocus(); // see Fixture::Manager()'s own doc comment

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
    REQUIRE(&manager.FocusedActiveBuffer().Get() == &b);                           // MRU pick -- not "a", list order's first non-closing entry
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
    manager.TakeFocus(); // see Fixture::Manager()'s own doc comment

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
    manager.TakeFocus(); // see Fixture::Manager()'s own doc comment

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
    manager.TakeFocus(); // see Fixture::Manager()'s own doc comment
    ned::ui::Widget& root = manager.RootComponent();

    FeedSequence(root, {ned::ui::test::Ctrl('x'), ned::ui::test::Character("2")});
    REQUIRE(manager.WindowCount() == 2);
}

TEST_CASE("Switching a pane's active buffer resolves a fresh Mode for the new buffer, per-buffer-mode follow-up",
          "[WindowManager]") {
    Fixture                fixture;
    ned::ui::WindowManager manager = fixture.Manager();
    manager.TakeFocus(); // see Fixture::Manager()'s own doc comment

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
    manager.TakeFocus(); // see Fixture::Manager()'s own doc comment

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

TEST_CASE("RefreshVcsDiffGutters requests a fresh diff for every live pane, not just the focused one",
          "[WindowManager][Vcs]") {
    // vcs-diff-gutter-staleness follow-up: WindowManager's own sweep (the
    // autosave-timer tick, and toggle-terminal's closing edge) must reach
    // every open pane, not only the focused one -- a split showing the same
    // file in two panes should refresh both.
    ned::editor::vcs::ClearRegistry();
    int diffRequests = 0;
    ned::editor::vcs::RegisterProvider("counting", std::make_unique<CountingDiffProvider>(diffRequests));
    const auto previousRoot = ned::editor::ProjectRoot();
    ned::editor::SetProjectRoot("/tmp");

    {
        Fixture fixture;
        fixture.buffer.SetPath("/tmp/ned-window-manager-diff-test.c");
        fixture.buffer.InsertAtPoint("hello");

        ned::ui::WindowManager manager = fixture.Manager();
        manager.TakeFocus();

        ned::ui::EventLoop          eventLoop;
        ned::editor::vcs::VcsRunner runner(eventLoop);
        manager.SetVcsRunner(&runner);

        manager.RefreshVcsDiffGutters();
        REQUIRE(diffRequests == 1); // one window so far

        ned::ui::Widget& root = manager.RootComponent();
        FeedSequence(root, {ned::ui::test::Ctrl('x'), ned::ui::test::Character("2")}); // split-window-below
        REQUIRE(manager.WindowCount() == 2);

        manager.RefreshVcsDiffGutters();
        REQUIRE(diffRequests == 3); // +2 -- both panes (same underlying buffer) refreshed
    }

    ned::editor::SetProjectRoot(previousRoot);
    ned::editor::vcs::ClearRegistry();
}

TEST_CASE("CaptureWindowLayout captures a split tree and the focused leaf's path", "[WindowManager]") {
    Fixture                fixture;
    ned::ui::WindowManager manager = fixture.Manager();
    manager.TakeFocus();

    const std::filesystem::path pathA =
        std::filesystem::temp_directory_path() / "ned_window_manager_test_capture_a.txt";
    const std::filesystem::path pathB =
        std::filesystem::temp_directory_path() / "ned_window_manager_test_capture_b.txt";
    ned::text::Buffer& bufferA = fixture.bufferList.OpenOrCreateFile(pathA);
    ned::text::Buffer& bufferB = fixture.bufferList.OpenOrCreateFile(pathB);

    ned::ui::Widget& root = manager.RootComponent();
    manager.FocusedActiveBuffer().Set(bufferA);
    FeedSequence(root, {ned::ui::test::Ctrl('x'), ned::ui::test::Character("3")}); // SplitRight -- focus stays on this (now bufferA) pane
    FeedSequence(root, {ned::ui::test::Ctrl('x'), ned::ui::test::Character("o")}); // focus the new (second) pane
    manager.FocusedActiveBuffer().Set(bufferB);
    FeedSequence(root, {ned::ui::test::Ctrl('x'), ned::ui::test::Character("o")}); // back to the original (bufferA, "first") pane

    ned::editor::ProjectSessionData data;
    manager.CaptureWindowLayout(data);

    // Post-order: two leaves then the split, so the split (the root) is the
    // last element -- see WindowLayoutNode's own doc comment.
    REQUIRE(data.windowLayout.size() == 3);
    const ned::editor::WindowLayoutNode& rootNode = data.windowLayout.back();
    REQUIRE(rootNode.kind == ned::editor::WindowLayoutNode::Kind::SplitRight);
    REQUIRE(rootNode.first.has_value());
    REQUIRE(rootNode.second.has_value());
    REQUIRE(data.windowLayout[*rootNode.first].file == std::filesystem::absolute(pathA));
    REQUIRE(data.windowLayout[*rootNode.second].file == std::filesystem::absolute(pathB));
    // The original ("first") pane, showing bufferA, had focus at capture time.
    REQUIRE(data.focusedPanePath == std::vector<int>{0});
}

TEST_CASE("CaptureWindowLayout leaves windowLayout empty when a leaf's buffer has no path", "[WindowManager]") {
    Fixture                fixture; // fixture.buffer ("scratch") has no path
    ned::ui::WindowManager manager = fixture.Manager();
    manager.TakeFocus();

    ned::editor::ProjectSessionData data;
    manager.CaptureWindowLayout(data);

    REQUIRE(data.windowLayout.empty());
}

TEST_CASE("RestoreWindowLayout rebuilds a split tree and restores the recorded focus", "[WindowManager]") {
    Fixture fixture;

    const std::filesystem::path pathA =
        std::filesystem::temp_directory_path() / "ned_window_manager_test_restore_a.txt";
    const std::filesystem::path pathB =
        std::filesystem::temp_directory_path() / "ned_window_manager_test_restore_b.txt";
    fixture.bufferList.OpenOrCreateFile(pathA);
    ned::text::Buffer& bufferB = fixture.bufferList.OpenOrCreateFile(pathB);

    ned::editor::WindowLayoutNode leafA;
    leafA.kind = ned::editor::WindowLayoutNode::Kind::Leaf;
    leafA.file = std::filesystem::absolute(pathA);
    ned::editor::WindowLayoutNode leafB;
    leafB.kind = ned::editor::WindowLayoutNode::Kind::Leaf;
    leafB.file = std::filesystem::absolute(pathB);
    ned::editor::WindowLayoutNode split;
    split.kind   = ned::editor::WindowLayoutNode::Kind::SplitRight;
    split.first  = 0;
    split.second = 1;

    ned::editor::ProjectSessionData data;
    data.windowLayout    = {leafA, leafB, split};
    data.focusedPanePath = {1}; // bufferB, the split's "second"

    ned::ui::WindowManager manager = fixture.Manager();
    manager.RestoreWindowLayout(data);

    REQUIRE(manager.WindowCount() == 2);
    REQUIRE(&manager.FocusedActiveBuffer().Get() == &bufferB);
}

TEST_CASE("RestoreWindowLayout is a no-op when windowLayout is empty", "[WindowManager]") {
    Fixture                fixture;
    ned::ui::WindowManager manager = fixture.Manager();
    manager.TakeFocus();

    ned::editor::ProjectSessionData data; // windowLayout left empty
    manager.RestoreWindowLayout(data);

    REQUIRE(manager.WindowCount() == 1);
}

TEST_CASE("RestoreWindowLayout leaves the existing default pane alone when a referenced file isn't open",
          "[WindowManager]") {
    Fixture                fixture;
    ned::ui::WindowManager manager = fixture.Manager();
    manager.TakeFocus();

    ned::editor::WindowLayoutNode leaf;
    leaf.kind = ned::editor::WindowLayoutNode::Kind::Leaf;
    leaf.file = "/definitely/not/open/anywhere/ned-window-layout-test.txt";

    ned::editor::ProjectSessionData data;
    data.windowLayout = {leaf};

    manager.RestoreWindowLayout(data);

    REQUIRE(manager.WindowCount() == 1);
}

// Split-resize follow-up: a fresh 50/50 SplitRight puts its column divider
// at x == lround(0.5 * available), where available = screen width - 1 (the
// divider's own 1 cell) -- the same math BuildComponent's firstSize lambda
// and SplitDivider::UpdateResize both use. Screen width is deliberately 81,
// not the round 80 an 80-column terminal would suggest: with an 80-wide
// screen, available (79) is odd, so 0.5 * 79 == 39.5 lands exactly on a
// rounding tie, and a subsequent drag's own delta/available division can
// round-trip a hair either side of the *next* tie depending on
// platform-specific float rounding noise -- a real, confirmed flake this
// test hit before switching to an even `available`, where every target
// lands on a plain integer (nowhere near a tie) and small float noise can
// never flip which way lround() rounds. Verified directly against the
// rendered '│' glyph rather than a new test-only ratio accessor, matching
// this file's own existing style of asserting on rendered/observable
// behavior (e.g. the tree-order regression test above) rather than reaching
// into WindowManager's internals.
TEST_CASE("Dragging a SplitRight divider resizes the split", "[WindowManager][SplitResize]") {
    Fixture                fixture;
    ned::ui::WindowManager manager = fixture.Manager();
    manager.TakeFocus();

    ned::ui::Widget& root = manager.RootComponent();
    FeedSequence(root, {ned::ui::test::Ctrl('x'), ned::ui::test::Character("3")}); // SplitRight
    REQUIRE(manager.WindowCount() == 2);

    ned::ui::Screen screen = ned::ui::Screen(81, 24); // available == 80, even -- see this test's own header comment
    RenderFullScreen(root, screen);
    REQUIRE(screen.PixelAt(40, 0).character == "\xe2\x94\x82"); // '│'

    root.OnEvent(ned::ui::test::Mouse(40, 5, ned::ui::MouseEvent::Button::Left, ned::ui::MouseEvent::Motion::Pressed));
    root.OnEvent(ned::ui::test::Mouse(50, 5, ned::ui::MouseEvent::Button::None, ned::ui::MouseEvent::Motion::Moved)); // +10 columns
    root.OnEvent(ned::ui::test::Mouse(50, 5, ned::ui::MouseEvent::Button::Left, ned::ui::MouseEvent::Motion::Released));

    screen = ned::ui::Screen(81, 24);
    RenderFullScreen(root, screen);
    REQUIRE(screen.PixelAt(50, 0).character == "\xe2\x94\x82"); // the divider followed the drag 1:1
    REQUIRE(screen.PixelAt(40, 0).character != "\xe2\x94\x82"); // and isn't still at the old spot too
}

TEST_CASE("Dragging a SplitBelow divider resizes the split", "[WindowManager][SplitResize]") {
    Fixture                fixture;
    ned::ui::WindowManager manager = fixture.Manager();
    manager.TakeFocus();

    ned::ui::Widget& root = manager.RootComponent();
    FeedSequence(root, {ned::ui::test::Ctrl('x'), ned::ui::test::Character("2")}); // SplitBelow
    REQUIRE(manager.WindowCount() == 2);

    // Height 25, not 24 -- available (24) needs to be even for the same
    // reason the SplitRight test above uses width 81; see its own header
    // comment.
    ned::ui::Screen screen = ned::ui::Screen(80, 25);
    RenderFullScreen(root, screen);
    REQUIRE(screen.PixelAt(0, 12).character == "\xe2\x94\x80"); // '─', lround(0.5 * 24) == 12

    root.OnEvent(ned::ui::test::Mouse(0, 12, ned::ui::MouseEvent::Button::Left, ned::ui::MouseEvent::Motion::Pressed));
    root.OnEvent(ned::ui::test::Mouse(0, 8, ned::ui::MouseEvent::Button::None, ned::ui::MouseEvent::Motion::Moved)); // -4 rows
    root.OnEvent(ned::ui::test::Mouse(0, 8, ned::ui::MouseEvent::Button::Left, ned::ui::MouseEvent::Motion::Released));

    screen = ned::ui::Screen(80, 25);
    RenderFullScreen(root, screen);
    REQUIRE(screen.PixelAt(0, 8).character == "\xe2\x94\x80");
}

// Split-resize follow-up: while a divider is mid-drag, a stray move/release
// landing over a neighboring pane's own BufferView must not be misread as a
// text-selection drag there -- see BufferView::SetSplitResizeQuery's own
// header comment for the real cross-widget hazard this guards against
// (every leaf gets every event regardless of position, so the second pane's
// BufferView receives these too).
TEST_CASE("A split-divider drag passing over a neighboring pane doesn't start a selection there",
          "[WindowManager][SplitResize]") {
    Fixture                fixture;
    ned::ui::WindowManager manager = fixture.Manager();
    manager.TakeFocus();

    ned::ui::Widget& root = manager.RootComponent();
    FeedSequence(root, {ned::ui::test::Ctrl('x'), ned::ui::test::Character("3")}); // SplitRight

    ned::ui::Screen screen = ned::ui::Screen(80, 24);
    RenderFullScreen(root, screen);

    root.OnEvent(ned::ui::test::Mouse(40, 5, ned::ui::MouseEvent::Button::Left, ned::ui::MouseEvent::Motion::Pressed));
    // Deep into the *second* pane's own territory, not just past the divider.
    root.OnEvent(ned::ui::test::Mouse(70, 5, ned::ui::MouseEvent::Button::None, ned::ui::MouseEvent::Motion::Moved));
    root.OnEvent(ned::ui::test::Mouse(70, 5, ned::ui::MouseEvent::Button::Left, ned::ui::MouseEvent::Motion::Released));

    // fixture.buffer (shown in both panes, split-window semantics) never
    // picked up a mark/selection from the stray move -- it was still the
    // divider's own drag the whole time.
    REQUIRE_FALSE(fixture.buffer.HasMark());
}

TEST_CASE("enlarge-window-horizontally/shrink-window-horizontally nudge a SplitRight's ratio", "[WindowManager][SplitResize]") {
    Fixture                fixture;
    ned::ui::WindowManager manager = fixture.Manager();
    manager.TakeFocus();

    ned::ui::Widget& root = manager.RootComponent();
    FeedSequence(root, {ned::ui::test::Ctrl('x'), ned::ui::test::Character("3")}); // SplitRight, focus stays on "first"

    ned::ui::Screen screen = ned::ui::Screen(80, 24);
    RenderFullScreen(root, screen);
    REQUIRE(screen.PixelAt(40, 0).character == "\xe2\x94\x82");

    FeedSequence(root, {ned::ui::test::Ctrl('x'), ned::ui::test::Character("}")}); // enlarge-window-horizontally
    screen = ned::ui::Screen(80, 24);
    RenderFullScreen(root, screen);
    REQUIRE(screen.PixelAt(41, 0).character == "\xe2\x94\x82"); // focused ("first") pane grew

    FeedSequence(root, {ned::ui::test::Ctrl('x'), ned::ui::test::Character("{")}); // shrink-window-horizontally
    FeedSequence(root, {ned::ui::test::Ctrl('x'), ned::ui::test::Character("{")});
    screen = ned::ui::Screen(80, 24);
    RenderFullScreen(root, screen);
    // 0.52 - 0.02 - 0.02 == 0.48 -> lround(0.48 * 79) == 38, past where it started.
    REQUIRE(screen.PixelAt(38, 0).character == "\xe2\x94\x82");
}

TEST_CASE("enlarge-window is a no-op with no split along that axis", "[WindowManager][SplitResize]") {
    Fixture                fixture;
    ned::ui::WindowManager manager = fixture.Manager();
    manager.TakeFocus();

    ned::ui::Widget& root = manager.RootComponent();
    FeedSequence(root, {ned::ui::test::Ctrl('x'), ned::ui::test::Character("^")}); // enlarge-window: no SplitBelow exists at all

    REQUIRE(manager.WindowCount() == 1); // didn't crash, didn't split anything either
}

// Split-resize follow-up: the *nearest* matching-axis ancestor wins, not
// the root -- regression coverage for FindNearestSplitAncestor's own
// recurse-into-child-first shape. Tree: SplitRight(A, SplitBelow(B, C)),
// focused on C (nested two levels deep, on the "second" side of both
// splits).
TEST_CASE("Vertical/horizontal resize each walk up to their own nearest matching-axis split", "[WindowManager][SplitResize]") {
    Fixture                fixture;
    ned::ui::WindowManager manager = fixture.Manager();
    manager.TakeFocus();

    ned::ui::Widget& root = manager.RootComponent();
    FeedSequence(root, {ned::ui::test::Ctrl('x'), ned::ui::test::Character("3")}); // SplitRight -> A | (focus stays on A)
    FeedSequence(root, {ned::ui::test::Ctrl('x'), ned::ui::test::Character("o")}); // focus the second pane
    FeedSequence(root, {ned::ui::test::Ctrl('x'), ned::ui::test::Character("2")}); // split it below -> A | (B / C), focus stays on B
    FeedSequence(root, {ned::ui::test::Ctrl('x'), ned::ui::test::Character("o")}); // focus C (innermost, "second" of both splits)
    REQUIRE(manager.WindowCount() == 3);

    // Outer SplitRight divider: x == lround(0.5 * (80 - 1)) == 40. Inner
    // SplitBelow divider: the "second" child of the outer split keeps the
    // full 0..39 row range (SplitRight only narrows width), so its own
    // y == lround(0.5 * (40 - 1)) == 20.
    ned::ui::Screen screen = ned::ui::Screen(80, 40);
    RenderFullScreen(root, screen);
    REQUIRE(screen.PixelAt(40, 0).character == "\xe2\x94\x82");
    REQUIRE(screen.PixelAt(60, 20).character == "\xe2\x94\x80");

    FeedSequence(root, {ned::ui::test::Ctrl('x'), ned::ui::test::Character("^")}); // enlarge-window (vertical) -- inner SplitBelow only
    screen = ned::ui::Screen(80, 40);
    RenderFullScreen(root, screen);
    REQUIRE(screen.PixelAt(40, 0).character == "\xe2\x94\x82"); // outer divider untouched
    // C is the inner split's "second" child, so growing it shrinks B's own
    // ratio (0.5 -> 0.48), moving the inner divider up by one row:
    // lround(0.48 * 39) == 19.
    REQUIRE(screen.PixelAt(60, 19).character == "\xe2\x94\x80");
    REQUIRE(screen.PixelAt(60, 20).character != "\xe2\x94\x80");

    FeedSequence(root, {ned::ui::test::Ctrl('x'), ned::ui::test::Character("}")}); // enlarge-window-horizontally -- outer SplitRight only
    screen = ned::ui::Screen(80, 40);
    RenderFullScreen(root, screen);
    // Same reasoning one level up: A's ratio shrinks 0.5 -> 0.48, moving the
    // outer divider from 40 to lround(0.48 * 79) == 38.
    REQUIRE(screen.PixelAt(38, 0).character == "\xe2\x94\x82");
    REQUIRE(screen.PixelAt(60, 19).character == "\xe2\x94\x80"); // inner divider untouched by the outer-axis command
}

TEST_CASE("A resized split's ratio survives CaptureWindowLayout + RestoreWindowLayout", "[WindowManager][SplitResize]") {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned_window_manager_test_resize_ratio.txt";

    ned::editor::ProjectSessionData data;
    {
        Fixture fixture;
        fixture.bufferList.OpenOrCreateFile(path);

        ned::ui::WindowManager manager = fixture.Manager();
        manager.TakeFocus();
        manager.FocusedActiveBuffer().Set(fixture.bufferList.OpenOrCreateFile(path));

        // Split-window semantics duplicate the buffer into the new pane
        // too, so both leaves end up pointing at `path` -- fine for
        // CaptureWindowLayout (each leaf is recorded independently) and one
        // less file this test needs to juggle.
        ned::ui::Widget& root = manager.RootComponent();
        FeedSequence(root, {ned::ui::test::Ctrl('x'), ned::ui::test::Character("3")}); // SplitRight

        // Width 81, not 80 -- available (80) needs to be even, same
        // rounding-tie reasoning as "Dragging a SplitRight divider resizes
        // the split"'s own header comment.
        ned::ui::Screen screen = ned::ui::Screen(81, 24);
        RenderFullScreen(root, screen);

        root.OnEvent(ned::ui::test::Mouse(40, 5, ned::ui::MouseEvent::Button::Left, ned::ui::MouseEvent::Motion::Pressed));
        root.OnEvent(ned::ui::test::Mouse(55, 5, ned::ui::MouseEvent::Button::None, ned::ui::MouseEvent::Motion::Moved));
        root.OnEvent(ned::ui::test::Mouse(55, 5, ned::ui::MouseEvent::Button::Left, ned::ui::MouseEvent::Motion::Released));

        manager.CaptureWindowLayout(data);
    }

    REQUIRE(data.windowLayout.size() == 3);
    REQUIRE(data.windowLayout.back().ratio != 0.5f); // the drag actually got captured, not just the default

    Fixture fixture2;
    fixture2.bufferList.OpenOrCreateFile(path);

    ned::ui::WindowManager manager2 = fixture2.Manager();
    manager2.RestoreWindowLayout(data);

    ned::ui::Widget& root2   = manager2.RootComponent();
    ned::ui::Screen  screen2 = ned::ui::Screen(81, 24);
    RenderFullScreen(root2, screen2);
    REQUIRE(screen2.PixelAt(55, 0).character == "\xe2\x94\x82"); // restored at the resized position, not back to 40
}
