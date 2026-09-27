#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>
#include <vector>

#include "Editor/Commands.h"
#include "Editor/Dispatcher.h"
#include "Editor/Keymap.h"
#include "Editor/Mode.h"
#include "Editor/Project/Root.h"
#include "Editor/PromptHistory.h"
#include "Editor/Register.h"
#include "Editor/Vim/Settings.h"
#include "TestEvents.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "Text/KillRing.h"
#include "UI/ActiveBuffer.h"
#include "UI/BufferView.h"
#include "UI/ListPopup.h"
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

class VimModeGuard {
  public:
    VimModeGuard() : previous_(ned::editor::vim::ModeEnabled()) {
        ned::editor::vim::SetModeEnabled(true);
    }
    ~VimModeGuard() {
        ned::editor::vim::SetModeEnabled(previous_);
    }
    VimModeGuard(const VimModeGuard&)            = delete;
    VimModeGuard& operator=(const VimModeGuard&) = delete;

  private:
    bool previous_;
};

class CommandCompletionGuard {
  public:
    explicit CommandCompletionGuard(bool enabled) : previous_(ned::editor::vim::CommandCompletionEnabled()) {
        ned::editor::vim::SetCommandCompletionEnabled(enabled);
    }
    ~CommandCompletionGuard() {
        ned::editor::vim::SetCommandCompletionEnabled(previous_);
    }
    CommandCompletionGuard(const CommandCompletionGuard&)            = delete;
    CommandCompletionGuard& operator=(const CommandCompletionGuard&) = delete;

  private:
    bool previous_;
};

void Type(BufferView& view, const std::string& keys) {
    for (const char c : keys) {
        view.OnEvent(c == '\n' ? ned::ui::test::Return() : ned::ui::test::Character(c));
    }
}

struct Harness {
    VimModeGuard                                 vimGuard;
    Fixture                                      fixture;
    BufferView                                   view = fixture.View();
    std::optional<ned::ui::ListPopupModel>       popup;
    std::vector<ned::editor::InteractiveRequest> requests;

    explicit Harness(const std::string& text) {
        fixture.buffer.InsertAtPoint(text);
        fixture.buffer.SetPoint(0);
        view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 9});
        view.SetOnCandidatesChanged([this](std::optional<ned::ui::ListPopupModel> model) { popup = std::move(model); });
        view.SetOnWindowRequest([this](ned::editor::InteractiveRequest request) { requests.push_back(request); });
    }

    [[nodiscard]] std::string SelectedName() const {
        REQUIRE(popup);
        REQUIRE(popup->selectedIndex);
        return popup->rows[*popup->selectedIndex].main;
    }
};

} // namespace

TEST_CASE("Typing an ex command name lists matching commands with their docs", "[BufferView][Vim]") {
    Harness h("one\n");

    Type(h.view, ":");
    CHECK_FALSE(h.popup); // nothing typed yet

    Type(h.view, "so");
    REQUIRE(h.popup);
    REQUIRE_FALSE(h.popup->rows.empty());
    CHECK(h.SelectedName() == "sort");
    CHECK_FALSE(h.popup->rows[*h.popup->selectedIndex].right.empty());
}

TEST_CASE("The completion popup closes once the command name is finished", "[BufferView][Vim]") {
    Harness h("one\n");

    Type(h.view, ":sp");
    REQUIRE(h.popup);
    Type(h.view, " ");
    CHECK_FALSE(h.popup);

    h.view.OnEvent(ned::ui::test::Escape());

    Type(h.view, ":q");
    REQUIRE(h.popup);
    h.view.OnEvent(ned::ui::test::Escape());
    CHECK_FALSE(h.popup);
}

TEST_CASE("Tab inserts the selected command name and keeps the range", "[BufferView][Vim]") {
    Harness h("banana\napple\ncherry\n");

    Type(h.view, ":%so");
    h.view.OnEvent(ned::ui::test::Tab());
    CHECK(h.fixture.statusMessage == ":%sort");

    Type(h.view, "\n");
    CHECK(h.fixture.buffer.Text() == "apple\nbanana\ncherry\n");
    CHECK_FALSE(h.popup);
}

TEST_CASE("Arrow keys and C-n/C-p move the completion selection", "[BufferView][Vim]") {
    Harness h("one\n");

    Type(h.view, ":sp");
    REQUIRE(h.popup);
    REQUIRE(h.popup->rows.size() >= 2);
    const std::string first = h.SelectedName();

    h.view.OnEvent(ned::ui::test::ArrowDown());
    const std::string second = h.SelectedName();
    CHECK(second != first);
    h.view.OnEvent(ned::ui::test::ArrowUp());
    CHECK(h.SelectedName() == first);

    h.view.OnEvent(ned::ui::test::Ctrl('n'));
    CHECK(h.SelectedName() == second);
    h.view.OnEvent(ned::ui::test::Ctrl('p'));
    CHECK(h.SelectedName() == first);
    CHECK(h.fixture.statusMessage == ":sp"); // moving the selection never edits the line
}

TEST_CASE("Enter runs the line as typed, not the selected completion", "[BufferView][Vim]") {
    Harness h("one\n");

    Type(h.view, ":sp");
    REQUIRE(h.SelectedName() == "split");
    h.view.OnEvent(ned::ui::test::ArrowDown());
    REQUIRE(h.SelectedName() != "split");

    Type(h.view, "\n");
    REQUIRE(h.requests.size() == 1);
    CHECK(h.requests[0] == ned::editor::InteractiveRequest::SplitBelow);
    CHECK_FALSE(h.popup);
}

TEST_CASE("A search line never shows ex command completion", "[BufferView][Vim]") {
    Harness h("sort\n");

    Type(h.view, "/so");
    CHECK_FALSE(h.popup);
}

TEST_CASE("ned/set-vim-command-completion false keeps the popup away", "[BufferView][Vim]") {
    CommandCompletionGuard off(false);
    Harness                h("banana\napple\n");

    Type(h.view, ":%so");
    CHECK_FALSE(h.popup);

    h.view.OnEvent(ned::ui::test::Tab()); // not consumed: the line is unchanged
    CHECK(h.fixture.statusMessage == ":%so");
}
