#include <catch2/catch_test_macros.hpp>

#include <string>

#include "Editor/Commands.h"
#include "Editor/Dispatcher.h"
#include "Editor/Keymap.h"
#include "Editor/Mode.h"
#include "Editor/PromptHistory.h"
#include "Editor/Register.h"
#include "TestEvents.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "Text/KillRing.h"
#include "UI/ActiveBuffer.h"
#include "UI/BufferView.h"
#include "UI/Theme.h"

// The per-hunk action chips painted after a "<<<<<<<" marker line, and the
// clicks that run them. Painted-screen tests rather than model ones, for the
// quick-fix column's own reason: a click has to land on the cell the user
// actually sees, so the column a chip occupies is the thing under test, not
// a recomputed guess at it.

using ned::ui::BufferView;

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
    ned::editor::Keymap          keymap = ned::editor::BuildDefaultGlobalKeymap();
    ned::editor::Dispatcher      dispatcher{registry, ned::editor::KeymapStack({&keymap})};
    ned::editor::Mode            mode  = ned::editor::FundamentalMode();
    ned::ui::Theme               theme = ned::ui::DarkTheme();

    std::string           statusMessage;
    ned::ui::ActiveBuffer activeBuffer{buffer};

    BufferView View() {
        return BufferView(activeBuffer, killRing, registers, promptHistory, bufferList, dispatcher, statusMessage,
                          mode, theme);
    }
};

void PaintInto(BufferView& view, ned::ui::Screen& screen) {
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 7});
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 7});
    view.Paint(canvas);
}

std::string PaintedRow(ned::ui::Screen& screen, int row, int width = 80) {
    std::string painted;
    for (int x = 0; x < width; ++x) {
        painted += screen.PixelAt(x, row).character;
    }
    return painted;
}

// The column a chip's own first cell landed in, read back off the painted
// row rather than computed -- the click has to hit what was drawn. Scanned
// cell by cell rather than by searching the joined row, because a joined
// row's byte offsets stop being columns the moment a multi-byte gutter
// glyph is painted to the left of the chips.
int ChipColumn(ned::ui::Screen& screen, int row, const std::string& label, int width = 80) {
    for (int x = 0; x + static_cast<int>(label.size()) <= width; ++x) {
        std::string at;
        for (int i = 0; i < static_cast<int>(label.size()); ++i) {
            at += screen.PixelAt(x + i, row).character;
        }
        if (at == label) {
            return x;
        }
    }
    return -1;
}

} // namespace

TEST_CASE("A conflict hunk paints its action chips after the marker line", "[BufferView][ConflictResolution]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("before\n<<<<<<< a\nours\n=======\ntheirs\n>>>>>>> b\nafter\n");
    fixture.buffer.SetPoint(0);
    BufferView      view = fixture.View();
    ned::ui::Screen screen(80, 8);
    PaintInto(view, screen);

    const std::string markerRow = PaintedRow(screen, 1);
    CHECK(markerRow.find("<<<<<<< a") != std::string::npos);
    CHECK(markerRow.find("[ours]") != std::string::npos);
    CHECK(markerRow.find("[theirs]") != std::string::npos);
    CHECK(markerRow.find("[both]") != std::string::npos);
    CHECK(markerRow.find("[neither]") != std::string::npos);
    CHECK(markerRow.find("[base]") == std::string::npos); // not a diff3 hunk

    // Ours/theirs carry the same washes their own content spans do.
    CHECK(screen.PixelAt(ChipColumn(screen, 1, "[ours]"), 1).background_color == fixture.theme.conflictOursBackground);
    CHECK(screen.PixelAt(ChipColumn(screen, 1, "[theirs]"), 1).background_color ==
          fixture.theme.conflictTheirsBackground);

    // Nothing anywhere else: the chips are the marker line's own, and no
    // line moved to make room for them.
    CHECK(PaintedRow(screen, 2).find("[ours]") == std::string::npos);
    CHECK(PaintedRow(screen, 2).find("ours") != std::string::npos);
}

TEST_CASE("A diff3 hunk gets a [base] chip too", "[BufferView][ConflictResolution]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("<<<<<<< a\nours\n||||||| base\nbase\n=======\ntheirs\n>>>>>>> b\n");
    fixture.buffer.SetPoint(0);
    BufferView      view = fixture.View();
    ned::ui::Screen screen(80, 8);
    PaintInto(view, screen);

    CHECK(PaintedRow(screen, 0).find("[base]") != std::string::npos);
}

TEST_CASE("A click on a chip resolves that hunk", "[BufferView][ConflictResolution]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("before\n<<<<<<< a\nours\n=======\ntheirs\n>>>>>>> b\nafter\n");
    fixture.buffer.SetPoint(0);
    BufferView      view = fixture.View();
    ned::ui::Screen screen(80, 8);
    PaintInto(view, screen);

    const int column = ChipColumn(screen, 1, "[theirs]");
    REQUIRE(column > 0);
    view.OnEvent(ned::ui::test::Mouse(column + 1, 1, ned::ui::MouseEvent::Button::Left,
                                      ned::ui::MouseEvent::Motion::Pressed));

    REQUIRE(fixture.buffer.Text() == "before\ntheirs\nafter\n");
}

TEST_CASE("A click just past the chips still places point", "[BufferView][ConflictResolution]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("before\n<<<<<<< a\nours\n=======\ntheirs\n>>>>>>> b\nafter\n");
    fixture.buffer.SetPoint(0);
    BufferView      view = fixture.View();
    ned::ui::Screen screen(80, 8);
    PaintInto(view, screen);

    const int pastEnd = ChipColumn(screen, 1, "[neither]") + 10;
    view.OnEvent(ned::ui::test::Mouse(pastEnd, 1, ned::ui::MouseEvent::Button::Left,
                                      ned::ui::MouseEvent::Motion::Pressed));

    REQUIRE(fixture.buffer.Text() == "before\n<<<<<<< a\nours\n=======\ntheirs\n>>>>>>> b\nafter\n");
    CHECK(fixture.buffer.Content().ByteOffsetToLine(fixture.buffer.Point()) == 1);
}

TEST_CASE("A read-only buffer showing marker text gets no chips", "[BufferView][ConflictResolution]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("<<<<<<< a\nours\n=======\ntheirs\n>>>>>>> b\n");
    fixture.buffer.SetPoint(0);
    fixture.buffer.SetReadOnly(true);
    BufferView      view = fixture.View();
    ned::ui::Screen screen(80, 8);
    PaintInto(view, screen);

    CHECK(PaintedRow(screen, 0).find("[ours]") == std::string::npos);
}

// The VCS gate. Marker text alone is also what a file *about* merge
// conflicts looks like (documentation, a test fixture, this codebase's own
// ROADMAP), so the chrome defers to the VCS's own answer once it has one.

TEST_CASE("A buffer the VCS reports as unconflicted gets no chips and no tinting",
          "[BufferView][ConflictResolution]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("<<<<<<< a\nours\n=======\ntheirs\n>>>>>>> b\n");
    fixture.buffer.SetPoint(0);
    BufferView view = fixture.View();

    ned::ui::Screen unknown(80, 8);
    PaintInto(view, unknown);
    REQUIRE(PaintedRow(unknown, 0).find("[ours]") != std::string::npos);
    // The "ours" content line carries the ours wash while the gate says
    // nothing -- read off the painted row, since the gutter sits left of it.
    const int oursColumn = ChipColumn(unknown, 1, "ours");
    REQUIRE(oursColumn > 0);
    REQUIRE(unknown.PixelAt(oursColumn, 1).background_color == fixture.theme.conflictOursBackground);

    view.DispatchConflictVerdictForTesting(ned::ui::bufferview::GutterModel::VcsConflictVerdict::Clean);
    ned::ui::Screen clean(80, 8);
    PaintInto(view, clean);

    CHECK(PaintedRow(clean, 0).find("[ours]") == std::string::npos);
    CHECK(PaintedRow(clean, 0).find("<<<<<<< a") != std::string::npos); // the text itself is untouched
    CHECK(clean.PixelAt(oursColumn, 1).background_color != fixture.theme.conflictOursBackground);
}

TEST_CASE("A buffer the VCS confirms as unmerged keeps its chips", "[BufferView][ConflictResolution]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("<<<<<<< a\nours\n=======\ntheirs\n>>>>>>> b\n");
    fixture.buffer.SetPoint(0);
    BufferView view = fixture.View();

    view.DispatchConflictVerdictForTesting(ned::ui::bufferview::GutterModel::VcsConflictVerdict::Conflicted);
    ned::ui::Screen screen(80, 8);
    PaintInto(view, screen);

    CHECK(PaintedRow(screen, 0).find("[ours]") != std::string::npos);
}

TEST_CASE("A verdict arriving after the hunks were derived still takes effect",
          "[BufferView][ConflictResolution]") {
    // The cache is keyed on content generation, which the verdict's own
    // arrival does not bump -- a gate checked outside the key would keep
    // painting the chips it just disowned.
    Fixture fixture;
    fixture.buffer.InsertAtPoint("<<<<<<< a\nours\n=======\ntheirs\n>>>>>>> b\n");
    fixture.buffer.SetPoint(0);
    BufferView view = fixture.View();

    ned::ui::Screen first(80, 8);
    PaintInto(view, first); // derives (and memoizes) the hunks at this generation
    REQUIRE(PaintedRow(first, 0).find("[ours]") != std::string::npos);

    view.DispatchConflictVerdictForTesting(ned::ui::bufferview::GutterModel::VcsConflictVerdict::Clean);
    ned::ui::Screen second(80, 8);
    PaintInto(view, second);
    CHECK(PaintedRow(second, 0).find("[ours]") == std::string::npos);

    // ...and back, for the same reason in the other direction.
    view.DispatchConflictVerdictForTesting(ned::ui::bufferview::GutterModel::VcsConflictVerdict::Conflicted);
    ned::ui::Screen third(80, 8);
    PaintInto(view, third);
    CHECK(PaintedRow(third, 0).find("[ours]") != std::string::npos);
}

// auto-conflict-resolution-nudge: a buffer becoming active/getting reported
// unmerged while already open should jump point to the first hunk and nudge
// the status line, exactly like opening a known-conflicted file from the VCS
// panel already did -- see ApplyConflictVerdict's own doc comment. Verdicts
// arrive on the same DispatchConflictVerdictForTesting seam the chip tests
// above use, since the real `git status` round trip needs a live EventLoop.

TEST_CASE("A newly-Conflicted verdict jumps point to the first hunk and nudges the status line",
          "[BufferView][ConflictResolution]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("before\n<<<<<<< a\nours\n=======\ntheirs\n>>>>>>> b\n");
    fixture.buffer.SetPoint(0);
    BufferView view = fixture.View();

    view.DispatchConflictVerdictForTesting(ned::ui::bufferview::GutterModel::VcsConflictVerdict::Conflicted);

    CHECK(fixture.buffer.Point() == std::string("before\n").size()); // the "<<<<<<<" byte, not 0
    CHECK(fixture.statusMessage == "merge conflict -- C-c x n/p to navigate, o/t/b/d/k to resolve");
}

TEST_CASE("Re-affirming an already-Conflicted verdict does not re-jump point",
          "[BufferView][ConflictResolution]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("before\n<<<<<<< a\nours\n=======\ntheirs\n>>>>>>> b\n");
    fixture.buffer.SetPoint(0);
    BufferView view = fixture.View();

    view.DispatchConflictVerdictForTesting(ned::ui::bufferview::GutterModel::VcsConflictVerdict::Conflicted);
    REQUIRE(fixture.buffer.Point() != 0); // the initial jump happened

    fixture.buffer.SetPoint(3); // simulate the user having moved point mid-resolution
    fixture.statusMessage.clear();

    // A debounce re-poll while nothing changed must never yank point back --
    // this is the whole point of gating on the Unknown/Clean -> Conflicted
    // transition rather than on the verdict alone.
    view.DispatchConflictVerdictForTesting(ned::ui::bufferview::GutterModel::VcsConflictVerdict::Conflicted);
    CHECK(fixture.buffer.Point() == 3);
    CHECK(fixture.statusMessage.empty());

    // ...but a real transition (an external merge finishing cleanly, then a
    // *new* conflict arising later) still jumps again.
    view.DispatchConflictVerdictForTesting(ned::ui::bufferview::GutterModel::VcsConflictVerdict::Clean);
    fixture.buffer.SetPoint(3);
    view.DispatchConflictVerdictForTesting(ned::ui::bufferview::GutterModel::VcsConflictVerdict::Conflicted);
    CHECK(fixture.buffer.Point() == std::string("before\n").size());
    CHECK(fixture.statusMessage == "merge conflict -- C-c x n/p to navigate, o/t/b/d/k to resolve");
}
