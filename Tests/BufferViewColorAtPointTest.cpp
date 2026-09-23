//
// color-at-point (C-c #): the edit half of the colour swatches.
//
// The invariant worth pinning is the one that makes the list safe to accept
// from: every row is a different spelling of the *same* colour, so whichever
// one is chosen, re-reading the buffer finds the colour it started with.
//

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "Editor/ColorLiteral.h"
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
using ned::ui::ListPopupModel;

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

    std::string                   statusMessage;
    ned::ui::ActiveBuffer         activeBuffer{buffer};
    std::optional<ListPopupModel> popup;

    // BufferView is neither copyable nor movable, so the fixture owns it
    // rather than handing one back by value.
    std::unique_ptr<BufferView> view;

    BufferView& View() {
        view = std::make_unique<BufferView>(activeBuffer, killRing, registers, promptHistory, bufferList,
                                            dispatcher, statusMessage, mode, theme);
        view->SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 23});
        view->SetOnCandidatesChanged([this](std::optional<ListPopupModel> model) { popup = std::move(model); });
        return *view;
    }
};

void InvokeColorAtPoint(BufferView& view) {
    view.OnEvent(ned::ui::test::Ctrl('c'));
    view.OnEvent(ned::ui::test::Character('#'));
}

std::vector<std::string> RowLabels(const ListPopupModel& model) {
    std::vector<std::string> labels;
    for (const ned::ui::ListPopupRow& row : model.rows) {
        labels.push_back(row.main);
    }
    return labels;
}

} // namespace

TEST_CASE("color-at-point offers every other notation for the colour under point", "[ColorAtPoint]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("#ff00aa\n");
    fixture.buffer.SetPoint(3);
    BufferView& view = fixture.View();

    InvokeColorAtPoint(view);

    REQUIRE(fixture.popup.has_value());
    const std::vector<std::string> labels = RowLabels(*fixture.popup);
    // Its own spelling is deliberately absent -- accepting it would be a
    // no-op edit that still lands in the undo tree.
    CHECK(std::find(labels.begin(), labels.end(), "#ff00aa") == labels.end());
    CHECK(std::find(labels.begin(), labels.end(), "rgb(255, 0, 170)") != labels.end());
    CHECK(std::find(labels.begin(), labels.end(), "hsl(320, 100%, 50%)") != labels.end());

    // Every row wears the colour it names.
    for (const ned::ui::ListPopupRow& row : fixture.popup->rows) {
        REQUIRE(row.leftForeground.has_value());
        CHECK(*row.leftForeground == ned::ui::Color::RGB(0xff00aa));
    }
}

TEST_CASE("Choosing a notation rewrites the literal in place", "[ColorAtPoint]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("color: #ff00aa;\n");
    fixture.buffer.SetPoint(10);
    BufferView& view = fixture.View();

    InvokeColorAtPoint(view);
    REQUIRE(fixture.popup.has_value());
    const std::string chosen = fixture.popup->rows.front().main;

    view.OnEvent(ned::ui::test::Character('1'));

    CHECK(fixture.buffer.Content().Substring(0, fixture.buffer.Size()) == "color: " + chosen + ";\n");
    // And it is still the same colour, which is the whole contract of the
    // list: re-reading the rewritten buffer finds what it started with.
    const std::vector<ned::editor::ColorLiteral> found =
        ned::editor::ScanColorLiterals("color: " + chosen + ";", {});
    REQUIRE(found.size() == 1);
    CHECK(ned::editor::FormatColor(found.front().color, ned::editor::ColorSyntax::HexAlpha) == "#ff00aaff");
}

TEST_CASE("Every offered notation round-trips when chosen", "[ColorAtPoint]") {
    // Walks the whole list rather than just the first row: a presentation
    // that reads back as a different colour would be silent data loss.
    std::size_t rowCount = 0;
    {
        Fixture fixture;
        fixture.buffer.InsertAtPoint("#3c6e8f\n");
        fixture.buffer.SetPoint(1);
        BufferView& view = fixture.View();
        InvokeColorAtPoint(view);
        REQUIRE(fixture.popup.has_value());
        rowCount = fixture.popup->rows.size();
    }
    REQUIRE(rowCount > 0);

    for (std::size_t row = 0; row < rowCount; ++row) {
        Fixture fixture;
        fixture.buffer.InsertAtPoint("#3c6e8f\n");
        fixture.buffer.SetPoint(1);
        BufferView& view = fixture.View();
        InvokeColorAtPoint(view);
        REQUIRE(fixture.popup.has_value());

        for (std::size_t step = 0; step < row; ++step) {
            view.OnEvent(ned::ui::test::ArrowDown());
        }
        view.OnEvent(ned::ui::test::Return());

        const std::string rewritten = fixture.buffer.Content().Substring(0, fixture.buffer.Size());
        INFO(rewritten);
        const std::vector<ned::editor::ColorLiteral> found = ned::editor::ScanColorLiterals(rewritten, {});
        REQUIRE(found.size() == 1);
        CHECK(ned::editor::FormatColor(found.front().color, ned::editor::ColorSyntax::HexAlpha) == "#3c6e8fff");
    }
}

TEST_CASE("color-at-point says so when point is not on a literal", "[ColorAtPoint]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("nothing to see here\n");
    fixture.buffer.SetPoint(4);
    BufferView& view = fixture.View();

    InvokeColorAtPoint(view);

    CHECK(fixture.statusMessage == "No colour literal at point.");
    CHECK(!fixture.popup.has_value());
}

TEST_CASE("A stylesheet's own spellings are offered only where the mode admits them", "[ColorAtPoint]") {
    SECTION("fundamental mode neither finds a bare colour name nor offers one") {
        Fixture fixture;
        fixture.buffer.InsertAtPoint("tomato\n");
        fixture.buffer.SetPoint(2);
        BufferView& view = fixture.View();
        InvokeColorAtPoint(view);
        CHECK(fixture.statusMessage == "No colour literal at point.");
    }

    SECTION("a stylesheet finds it, and offers the hex it stands for") {
        Fixture fixture;
        fixture.mode.colorLiterals = {.shortHex = true, .namedColors = true};
        fixture.buffer.InsertAtPoint("tomato\n");
        fixture.buffer.SetPoint(2);
        BufferView& view = fixture.View();
        InvokeColorAtPoint(view);

        REQUIRE(fixture.popup.has_value());
        const std::vector<std::string> labels = RowLabels(*fixture.popup);
        CHECK(std::find(labels.begin(), labels.end(), "#ff6347") != labels.end());
        CHECK(std::find(labels.begin(), labels.end(), "tomato") == labels.end());
    }
}
