#include <catch2/catch_test_macros.hpp>

#include <string>

#include "Editor/Commands.h"
#include "Editor/MergeView.h"
#include "Editor/Mode.h"
#include "Editor/PromptHistory.h"
#include "Editor/Register.h"
#include "TestEvents.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "Text/KillRing.h"
#include "UI/ActiveBuffer.h"
#include "UI/Theme.h"
#include "UI/WindowManager.h"

// The side-by-side merge view as WindowManager hosts it: opening and closing
// the three-pane layout, rows lining up across panes, scrolling together, and
// the conflict commands acting from a side pane.

using ned::editor::MergeSideBufferName;
using ned::text::MergeSideKind;
using namespace ned::ui::test;

namespace {

constexpr const char* kConflicted = "before\n<<<<<<< HEAD\nours 1\nours 2\n=======\ntheirs\n>>>>>>> feature\nafter\n";

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
    ned::editor::Keymap          janetKeymap;
    ned::ui::Theme               theme = ned::ui::DarkTheme();
    std::string                  statusMessage;

    ned::ui::WindowManager Manager() {
        return ned::ui::WindowManager(buffer, killRing, registers, promptHistory, bufferList, registry, janetKeymap,
                                      globalKeymap, ned::editor::FundamentalMode(), statusMessage, theme);
    }
};

void Feed(std::initializer_list<ned::ui::Event> events) {
    for (const ned::ui::Event& event : events) {
        if (ned::ui::Widget* focused = ned::ui::FocusedWidget()) {
            focused->OnEvent(event);
        }
    }
}

void ToggleMergeView() {
    Feed({Ctrl('c'), Character("x"), Character("v")});
}

// One frame the way main.cpp paints it.
void Frame(ned::ui::WindowManager& manager, ned::ui::Screen& screen) {
    ned::ui::Widget& root = manager.RootComponent();
    root.SetBox_(ned::ui::Box{.x_min = 0, .x_max = screen.Width() - 1, .y_min = 0, .y_max = screen.Height() - 1});
    manager.SyncMergeView();
    root.Paint(ned::ui::Canvas(screen, root.Box_()));
    if (manager.SyncMergeView()) {
        root.Paint(ned::ui::Canvas(screen, root.Box_()));
    }
}

std::string RowText(ned::ui::Screen& screen, int row) {
    std::string out;
    for (int col = 0; col < screen.Width(); ++col) {
        out += screen.PixelAt(col, row).character;
    }
    return out;
}

std::size_t Occurrences(const std::string& haystack, const std::string& needle) {
    std::size_t count = 0;
    for (std::size_t at = haystack.find(needle); at != std::string::npos; at = haystack.find(needle, at + 1)) {
        ++count;
    }
    return count;
}

ned::text::Buffer& OpenConflicted(Fixture& fixture, ned::ui::WindowManager& manager, const std::string& text) {
    ned::text::Buffer& merged = fixture.bufferList.CreateBuffer("main.cpp");
    merged.InsertAtPoint(text);
    merged.SetPoint(0);
    manager.FocusedActiveBuffer().Set(merged);
    ToggleMergeView();
    return merged;
}

void FocusBuffer(ned::ui::WindowManager& manager, const std::string& name) {
    for (int i = 0; i < 4 && manager.FocusedActiveBuffer().Get().Name() != name; ++i) {
        Feed({Ctrl('x'), Character("o")});
    }
    REQUIRE(manager.FocusedActiveBuffer().Get().Name() == name);
}

} // namespace

TEST_CASE("merge-view opens ours, merged and theirs side by side", "[WindowManager][MergeView]") {
    Fixture                fixture;
    ned::ui::WindowManager manager = fixture.Manager();
    manager.TakeFocus();
    ned::text::Buffer& merged = OpenConflicted(fixture, manager, kConflicted);

    REQUIRE(manager.MergeView() != nullptr);
    CHECK(manager.WindowCount() == 3);
    CHECK(&manager.FocusedActiveBuffer().Get() == &merged);
    CHECK(fixture.statusMessage.find("1 conflict") != std::string::npos);

    ned::ui::Screen screen(120, 16);
    Frame(manager, screen);
    // "after" is merged line 7 and the sides pad down to it.
    bool aligned = false;
    for (int row = 0; row < screen.Height(); ++row) {
        aligned = aligned || Occurrences(RowText(screen, row), "after") == 3;
    }
    CHECK(aligned);
}

TEST_CASE("merge-view again closes it and restores the previous layout", "[WindowManager][MergeView]") {
    Fixture                fixture;
    ned::ui::WindowManager manager = fixture.Manager();
    manager.TakeFocus();
    Feed({Ctrl('x'), Character("2")});
    REQUIRE(manager.WindowCount() == 2);
    ned::text::Buffer& merged = OpenConflicted(fixture, manager, kConflicted);
    REQUIRE(manager.WindowCount() == 3);

    FocusBuffer(manager, MergeSideBufferName(MergeSideKind::Theirs, "main.cpp"));
    ToggleMergeView();
    CHECK(manager.MergeView() == nullptr);
    CHECK(manager.WindowCount() == 2);
    CHECK(&manager.FocusedActiveBuffer().Get() == &merged);
    CHECK_FALSE(fixture.bufferList.Find(MergeSideBufferName(MergeSideKind::Ours, "main.cpp")));
    CHECK_FALSE(fixture.bufferList.Find(MergeSideBufferName(MergeSideKind::Theirs, "main.cpp")));
}

TEST_CASE("merge-view in a buffer without markers says so and changes nothing", "[WindowManager][MergeView]") {
    Fixture                fixture;
    ned::ui::WindowManager manager = fixture.Manager();
    manager.TakeFocus();
    OpenConflicted(fixture, manager, "no conflicts here\n");
    CHECK(manager.MergeView() == nullptr);
    CHECK(manager.WindowCount() == 1);
    CHECK(fixture.statusMessage == "no conflict hunks in this buffer");
}

TEST_CASE("Scrolling the merged pane scrolls the sides to the same rows", "[WindowManager][MergeView]") {
    Fixture                fixture;
    ned::ui::WindowManager manager = fixture.Manager();
    manager.TakeFocus();
    std::string text = kConflicted;
    for (int i = 100; i < 200; ++i) {
        text += "tok" + std::to_string(i) + "\n";
    }
    OpenConflicted(fixture, manager, text);
    ned::ui::Screen screen(120, 16);
    Frame(manager, screen);

    Feed({Alt('>')});
    Frame(manager, screen);
    const std::string top = RowText(screen, 0);
    const std::size_t at  = top.find("tok");
    REQUIRE(at != std::string::npos);
    const std::string token = top.substr(at, 6);
    CHECK(Occurrences(top, token) == 3);
}

TEST_CASE("From a side pane, n finds the hunk and C-c x a takes that side", "[WindowManager][MergeView]") {
    Fixture                fixture;
    ned::ui::WindowManager manager = fixture.Manager();
    manager.TakeFocus();
    ned::text::Buffer& merged = OpenConflicted(fixture, manager, kConflicted);
    ned::ui::Screen    screen(120, 16);
    Frame(manager, screen);

    FocusBuffer(manager, MergeSideBufferName(MergeSideKind::Ours, "main.cpp"));
    Feed({Ctrl('c'), Character("x"), Character("n")});
    Feed({Ctrl('c'), Character("x"), Character("a")});
    CHECK(merged.Text() == "before\nours 1\nours 2\nafter\n");
    CHECK(fixture.statusMessage.find("no conflicts left") != std::string::npos);
    // The view stays open over the resolved file until it is closed.
    Frame(manager, screen);
    CHECK(manager.MergeView() != nullptr);
}

TEST_CASE("From a side pane, the resolution chords act on the merged hunk", "[WindowManager][MergeView]") {
    Fixture                fixture;
    ned::ui::WindowManager manager = fixture.Manager();
    manager.TakeFocus();
    ned::text::Buffer& merged = OpenConflicted(fixture, manager, kConflicted);
    ned::ui::Screen    screen(120, 16);
    Frame(manager, screen);

    FocusBuffer(manager, MergeSideBufferName(MergeSideKind::Ours, "main.cpp"));
    Feed({Ctrl('c'), Character("x"), Character("n")});
    Feed({Ctrl('c'), Character("x"), Character("t")});
    CHECK(merged.Text() == "before\ntheirs\nafter\n");
}

TEST_CASE("Closing the merged buffer closes the merge view on the next frame", "[WindowManager][MergeView]") {
    Fixture                fixture;
    ned::ui::WindowManager manager = fixture.Manager();
    manager.TakeFocus();
    ned::text::Buffer& merged = OpenConflicted(fixture, manager, kConflicted);
    ned::ui::Screen    screen(120, 16);
    Frame(manager, screen);

    manager.NotifyBufferClosing(merged);
    fixture.bufferList.Close("main.cpp");
    Frame(manager, screen);
    CHECK(manager.MergeView() == nullptr);
    CHECK(manager.WindowCount() == 1);
    CHECK_FALSE(manager.FocusedActiveBuffer().Get().Name().starts_with("*merge "));
}

TEST_CASE("A merged pane scrolled into a hunk pads the sides' top so rows still line up", "[WindowManager][MergeView]") {
    Fixture                fixture;
    ned::ui::WindowManager manager = fixture.Manager();
    manager.TakeFocus();
    std::string text = kConflicted;
    for (int i = 100; i < 200; ++i) {
        text += "tok" + std::to_string(i) + "\n";
    }
    OpenConflicted(fixture, manager, text);
    ned::ui::Screen screen(120, 16);
    Frame(manager, screen);

    // Point on merged line 11, recentred in a 15-row pane: top line 4, the
    // "=======" inside the hunk. Ours and theirs have no line on that row,
    // so they start with blank rows above "after".
    for (int i = 0; i < 11; ++i) {
        Feed({Ctrl('n')});
    }
    Feed({Ctrl('l')});
    Frame(manager, screen);
    REQUIRE(RowText(screen, 0).find("=======") != std::string::npos);
    CHECK(Occurrences(RowText(screen, 0), "ours 2") == 0);
    CHECK(Occurrences(RowText(screen, 3), "after") == 3);
    CHECK(Occurrences(RowText(screen, 4), "tok100") == 3);
}

TEST_CASE("Side panes wash their hunk lines in their own conflict colour", "[WindowManager][MergeView]") {
    Fixture                fixture;
    ned::ui::WindowManager manager = fixture.Manager();
    manager.TakeFocus();
    OpenConflicted(fixture, manager, kConflicted);
    ned::ui::Screen screen(120, 16);
    Frame(manager, screen);

    // Background of the first cell of `text`'s first (or last) match on `row`.
    auto backgroundOf = [&](int row, const std::string& text, bool last) {
        const std::string painted = RowText(screen, row);
        const std::size_t at      = last ? painted.rfind(text) : painted.find(text);
        REQUIRE(at != std::string::npos);
        int column = 0;
        for (std::size_t byte = 0; byte < at; ++column) {
            byte += screen.PixelAt(column, row).character.size();
        }
        return screen.PixelAt(column, row).background_color;
    };
    CHECK(backgroundOf(1, "ours 1", false) == fixture.theme.conflictOursBackground);
    CHECK(backgroundOf(1, "theirs", true) == fixture.theme.conflictTheirsBackground);
    CHECK(backgroundOf(0, "before", false) != fixture.theme.conflictOursBackground);
    CHECK(backgroundOf(7, "after", true) != fixture.theme.conflictTheirsBackground);
}
