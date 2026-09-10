#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <thread>

#include "Editor/Commands.h"
#include "Editor/Dispatcher.h"
#include "Editor/Keymap.h"
#include "Editor/Mode.h"
#include "Editor/PromptHistory.h"
#include "Editor/RecencyGlow.h"
#include "Editor/Register.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "Text/KillRing.h"
#include "UI/ActiveBuffer.h"
#include "UI/BufferView.h"
#include "UI/Theme.h"

using ned::ui::BufferView;
using ned::ui::Color;

// Translucency phase 6: the recency glow. The load-bearing property is not
// that it appears -- it is that it *stops*, since ned's event loop has no
// free-running render tick and the animation re-arms itself only while
// something is still fading.

namespace {

struct GlowGuard {
    const bool previous = ned::editor::RecencyGlowEnabled();
    ~GlowGuard() {
        ned::editor::SetRecencyGlowEnabled(previous);
    }
};

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
    ned::editor::Mode            mode  = ned::editor::Mode{.name = "fundamental-mode"};
    ned::ui::Theme               theme = ned::ui::DarkTheme();

    std::string           statusMessage;
    ned::ui::ActiveBuffer activeBuffer{buffer};

    BufferView View() {
        return BufferView(activeBuffer, killRing, registers, promptHistory, bufferList, dispatcher, statusMessage,
                          mode, theme);
    }
};

void PaintOnce(BufferView& view) {
    const ned::ui::Box box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 3};
    view.SetBox_(box);
    ned::ui::Screen screen(40, 4);
    ned::ui::Canvas canvas(screen, box);
    view.Paint(canvas);
}

} // namespace

TEST_CASE("An edit starts a glow, and it expires on its own", "[RecencyGlow]") {
    const GlowGuard guard;
    ned::editor::SetRecencyGlowEnabled(true);

    Fixture    fixture;
    BufferView view = fixture.View();
    PaintOnce(view); // seeds

    fixture.buffer.InsertAtPoint("hello");
    PaintOnce(view);
    REQUIRE(view.HasLiveRecencyGlow());

    // The whole CPU story rests on this going false without anything asking
    // it to: while it is true the composition root re-arms a 60ms timer, and
    // when it is false nothing wakes the event loop at all.
    std::this_thread::sleep_for(ned::editor::kRecencyGlowDuration + std::chrono::milliseconds(80));
    REQUIRE_FALSE(view.HasLiveRecencyGlow());
}

TEST_CASE("A buffer's existing unsaved ranges do not glow when it is first painted", "[RecencyGlow]") {
    const GlowGuard guard;
    ned::editor::SetRecencyGlowEnabled(true);

    Fixture fixture;
    // Edited *before* this view ever painted -- a restored session, or simply
    // switching to a buffer someone was working in. None of that just
    // happened, so none of it should light up.
    fixture.buffer.InsertAtPoint("already edited before the first paint");

    BufferView view = fixture.View();
    PaintOnce(view);
    REQUIRE_FALSE(view.HasLiveRecencyGlow());

    // ...and a real edit after that still glows, so seeding did not simply
    // swallow the first one.
    fixture.buffer.InsertAtPoint("!");
    PaintOnce(view);
    REQUIRE(view.HasLiveRecencyGlow());
}

TEST_CASE("Disabling the glow stops it being live at all", "[RecencyGlow]") {
    const GlowGuard guard;
    ned::editor::SetRecencyGlowEnabled(false);

    Fixture    fixture;
    BufferView view = fixture.View();
    PaintOnce(view);
    fixture.buffer.InsertAtPoint("hello");
    PaintOnce(view);

    // False regardless of what was tracked, so the timer is never armed --
    // the switch has to reach the animation, not just the painting.
    REQUIRE_FALSE(view.HasLiveRecencyGlow());
}

TEST_CASE("A glow covers only the bytes the newest edit touched", "[RecencyGlow]") {
    const GlowGuard guard;
    ned::editor::SetRecencyGlowEnabled(true);

    Fixture    fixture;
    BufferView view = fixture.View();
    PaintOnce(view);

    // UnsavedChangeRanges merges, so typing continuously grows one range
    // rather than appending. Without subtracting the previous set, the
    // second edit would re-glow everything typed since the last save.
    fixture.buffer.InsertAtPoint("aaaaa");
    PaintOnce(view);
    std::this_thread::sleep_for(ned::editor::kRecencyGlowDuration + std::chrono::milliseconds(80));
    REQUIRE_FALSE(view.HasLiveRecencyGlow()); // the first edit's glow is gone

    fixture.buffer.InsertAtPoint("b");
    PaintOnce(view);
    REQUIRE(view.HasLiveRecencyGlow());

    // If the whole merged range had been re-stamped, the earlier text would
    // be glowing again too; painting shows only the newest byte lit.
    const ned::ui::Box box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 3};
    view.SetBox_(box);
    ned::ui::Screen screen(40, 4);
    ned::ui::Canvas canvas(screen, box);
    view.Paint(canvas);

    int glowing = 0;
    for (int x = 0; x < 40; ++x) {
        const ned::ui::Cell& cell = screen.PixelAt(x, 0);
        if ((cell.character == "a" || cell.character == "b") &&
            !(cell.background_color == fixture.theme.background)) {
            ++glowing;
        }
    }
    REQUIRE(glowing <= 1);
}
