//
// The two shared prompt drivers a large part of the editor now routes through:
// the yes/no confirmation (BufferView/ConfirmPrompt.h) and the type-to-narrow
// picker (BufferView/FuzzyPrompt.h).
//
// Worth testing directly because collapsing nine prompts onto one driver raised
// the blast radius of a mistake in it: a bug here is a bug in every prompt at
// once, where before it would have been a bug in one.
//

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

// A modified buffer, since closing an unmodified one never asks.
ned::text::Buffer& ModifiedBuffer(Fixture& fixture, const std::string& name) {
    ned::text::Buffer& b = fixture.bufferList.CreateBuffer(name);
    b.InsertAtPoint("unsaved work");
    return b;
}

void Type(BufferView& view, const std::string& text) {
    for (char ch : text) {
        view.OnEvent(ned::ui::test::Character(ch));
    }
}

} // namespace

// ---------------------------------------------------------------- confirmations

TEST_CASE("A confirmation asks before discarding unsaved work", "[BufferView][ConfirmPrompt]") {
    Fixture            fixture;
    BufferView         view   = fixture.View();
    ned::text::Buffer& target = ModifiedBuffer(fixture, "doomed");

    view.RequestCloseBuffer(target);

    REQUIRE(fixture.statusMessage.find("close anyway? (y/n)") != std::string::npos);
    REQUIRE(fixture.bufferList.Find("doomed") != nullptr); // nothing closed yet
}

TEST_CASE("Confirming runs the action", "[BufferView][ConfirmPrompt]") {
    Fixture    fixture;
    BufferView view = fixture.View();
    ModifiedBuffer(fixture, "doomed");

    view.RequestCloseBuffer(*fixture.bufferList.Find("doomed"));
    view.OnEvent(ned::ui::test::Character('y'));

    REQUIRE(fixture.bufferList.Find("doomed") == nullptr);
}

TEST_CASE("Declining leaves everything alone and says so", "[BufferView][ConfirmPrompt]") {
    Fixture    fixture;
    BufferView view = fixture.View();
    ModifiedBuffer(fixture, "doomed");

    view.RequestCloseBuffer(*fixture.bufferList.Find("doomed"));
    view.OnEvent(ned::ui::test::Character('n'));

    REQUIRE(fixture.bufferList.Find("doomed") != nullptr);
    REQUIRE(fixture.statusMessage == "Close cancelled.");
}

TEST_CASE("The quit chord declines too", "[BufferView][ConfirmPrompt]") {
    Fixture    fixture;
    BufferView view = fixture.View();
    ModifiedBuffer(fixture, "doomed");

    view.RequestCloseBuffer(*fixture.bufferList.Find("doomed"));
    view.OnEvent(ned::ui::test::Escape());

    REQUIRE(fixture.bufferList.Find("doomed") != nullptr);
    REQUIRE(fixture.statusMessage == "Close cancelled.");
}

TEST_CASE("An unrelated key neither confirms nor declines", "[BufferView][ConfirmPrompt]") {
    Fixture    fixture;
    BufferView view = fixture.View();
    ModifiedBuffer(fixture, "doomed");

    view.RequestCloseBuffer(*fixture.bufferList.Find("doomed"));
    // A question about discarding work must not be answered by a stray press.
    view.OnEvent(ned::ui::test::Character('k'));

    REQUIRE(fixture.bufferList.Find("doomed") != nullptr);
    REQUIRE(fixture.statusMessage.find("close anyway? (y/n)") != std::string::npos); // still asking
}

// ------------------------------------------------------------- fuzzy prompts

TEST_CASE("A fuzzy prompt narrows as you type and commits the match", "[BufferView][FuzzyPrompt]") {
    Fixture fixture;
    fixture.bufferList.CreateBuffer("alpha");
    fixture.bufferList.CreateBuffer("bravo");
    BufferView view = fixture.View();

    view.OnEvent(ned::ui::test::Ctrl('x')); // C-x b -- switch-to-buffer
    view.OnEvent(ned::ui::test::Character('b'));
    Type(view, "bravo");
    view.OnEvent(ned::ui::test::Return());

    REQUIRE(fixture.activeBuffer.Get().Name() == "bravo");
}

TEST_CASE("A fuzzy prompt reports when nothing matches, without switching", "[BufferView][FuzzyPrompt]") {
    Fixture fixture;
    fixture.bufferList.CreateBuffer("alpha");
    BufferView view = fixture.View();

    view.OnEvent(ned::ui::test::Ctrl('x'));
    view.OnEvent(ned::ui::test::Character('b'));
    Type(view, "zzzznope");
    view.OnEvent(ned::ui::test::Return());

    REQUIRE(fixture.statusMessage.find("No buffer matching") != std::string::npos);
    REQUIRE(fixture.activeBuffer.Get().Name() == "scratch");
}

TEST_CASE("Cancelling a fuzzy prompt commits nothing", "[BufferView][FuzzyPrompt]") {
    Fixture fixture;
    fixture.bufferList.CreateBuffer("alpha");
    BufferView view = fixture.View();

    view.OnEvent(ned::ui::test::Ctrl('x'));
    view.OnEvent(ned::ui::test::Character('b'));
    Type(view, "alpha");
    view.OnEvent(ned::ui::test::Escape());

    REQUIRE(fixture.statusMessage == "Switch to buffer cancelled.");
    REQUIRE(fixture.activeBuffer.Get().Name() == "scratch");
}

TEST_CASE("Typing re-snaps the selection to the best match", "[BufferView][FuzzyPrompt]") {
    // Two buffers that both survive the query, so a stale selection index picks
    // the wrong one rather than being clamped harmlessly back to the only match.
    const auto pick = [](bool moveSelectionFirst) {
        Fixture fixture;
        fixture.bufferList.CreateBuffer("alpha");
        fixture.bufferList.CreateBuffer("alpine");
        BufferView view = fixture.View();

        view.OnEvent(ned::ui::test::Ctrl('x'));
        view.OnEvent(ned::ui::test::Character('b'));
        Type(view, "al"); // narrows to alpha + alpine
        if (moveSelectionFirst) {
            view.OnEvent(ned::ui::test::ArrowDown()); // off the top match
        }
        Type(view, "p"); // typing again must re-snap to the top
        view.OnEvent(ned::ui::test::Return());
        return fixture.activeBuffer.Get().Name();
    };

    // Whatever the ranking puts first, having moved the highlight beforehand
    // must not change what typing then commits.
    REQUIRE(pick(/*moveSelectionFirst=*/true) == pick(/*moveSelectionFirst=*/false));
}
