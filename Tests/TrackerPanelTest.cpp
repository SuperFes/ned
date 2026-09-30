//
// TrackerPanel (Source/UI/TrackerPanel.h) -- headless, over its own Set*
// callbacks: fetching is main.cpp's wiring, so a test answers a fetch
// request by calling ShowIssues/ShowError itself.
//

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "TestEvents.h"
#include "UI/TrackerPanel.h"
#include "UI/Widget.h"

namespace {

using ned::editor::tracker::Issue;
using ned::ui::Box;
using ned::ui::Canvas;
using ned::ui::Screen;
using ned::ui::Theme;
using ned::ui::TrackerPanel;
namespace test = ned::ui::test;

constexpr int kWidth  = 48;
constexpr int kHeight = 16;

struct Fixture {
    Theme        theme = ned::ui::DarkTheme();
    TrackerPanel panel{theme, "Mine"};
    Screen       screen{kWidth, kHeight};

    int                      fetches = 0;
    std::vector<std::string> opened;
    std::vector<std::string> urls;
    std::vector<std::string> copied;
    std::vector<std::string> messages;
    int                      cancels = 0;

    Fixture() {
        panel.Tree().SetDrawBorder(false);
        panel.Tree().SetBox_(Box{.x_min = 0, .x_max = kWidth - 1, .y_min = 0, .y_max = kHeight - 1});
        panel.Tree().TakeFocus();
        panel.SetOnFetchRequested([this] { ++fetches; });
        panel.SetOnOpenIssue([this](const Issue& issue) { opened.push_back(issue.key); });
        panel.SetOnOpenUrl([this](const std::string& url) { urls.push_back(url); });
        panel.SetOnCopy([this](std::string text) { copied.push_back(std::move(text)); });
        panel.SetOnMessage([this](std::string message) { messages.push_back(std::move(message)); });
        panel.SetOnCancel([this] { ++cancels; });
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

    [[nodiscard]] std::string AllRows() {
        panel.Tree().Paint(Canvas(screen, panel.Tree().Box_()));
        std::string text;
        for (int y = 0; y < kHeight; ++y) {
            text += RowText(y) + "\n";
        }
        return text;
    }

    void Press(const ned::ui::Event& event) {
        REQUIRE(panel.Tree().OnEvent(event));
    }

    void Down(int n) {
        for (int i = 0; i < n; ++i) {
            Press(test::ArrowDown());
        }
    }
};

std::vector<Issue> SampleIssues() {
    return {Issue{.key = "NED-1", .title = "Crash on save", .status = "In Progress", .url = "https://x/NED-1"},
            Issue{.key = "NED-2", .title = "Tooltip clips", .status = "To Do"},
            Issue{.key = "NED-3", .title = "Slow start", .status = "In Progress", .url = "https://x/NED-3"}};
}

} // namespace

TEST_CASE("A tracker panel fetches the first time it is shown, and on g", "[TrackerPanel]") {
    Fixture f;
    CHECK(f.AllRows().find("Press g to load") != std::string::npos);

    f.panel.NotifyShown();
    CHECK(f.fetches == 1);
    CHECK(f.AllRows().find("Loading") != std::string::npos);
    // Still loading: neither showing again nor g starts a second fetch.
    f.panel.NotifyShown();
    f.Press(test::Character('g'));
    CHECK(f.fetches == 1);

    f.panel.ShowIssues({});
    CHECK(f.AllRows().find("(no issues)") != std::string::npos);
    f.panel.NotifyShown();
    CHECK(f.fetches == 1);
    f.Press(test::Character('g'));
    CHECK(f.fetches == 2);
}

TEST_CASE("Issues are grouped under their statuses in order of first appearance", "[TrackerPanel]") {
    Fixture f;
    f.panel.NotifyShown();
    f.panel.ShowIssues(SampleIssues());

    const std::string rows = f.AllRows();
    INFO(rows);
    const auto inProgress = rows.find("In Progress");
    const auto first      = rows.find("NED-1  Crash on save");
    const auto third      = rows.find("NED-3  Slow start");
    const auto toDo       = rows.find("To Do");
    const auto second     = rows.find("NED-2  Tooltip clips");
    REQUIRE(inProgress != std::string::npos);
    REQUIRE(toDo != std::string::npos);
    CHECK(inProgress < first);
    CHECK(first < third);
    CHECK(third < toDo);
    CHECK(toDo < second);
    CHECK(f.RowText(0).ends_with("2")); // the header's count
}

TEST_CASE("Collapsing a status hides its issues and survives a refresh", "[TrackerPanel]") {
    Fixture f;
    f.panel.NotifyShown();
    f.panel.ShowIssues(SampleIssues());

    f.Down(3); // the "To Do" header
    f.Press(test::ArrowLeft());
    std::string rows = f.AllRows();
    CHECK(rows.find("NED-2") == std::string::npos);
    CHECK(rows.find("NED-1") != std::string::npos);
    CHECK(f.panel.Tree().SelectedRow() == 3);

    f.Press(test::Character('g'));
    f.panel.ShowIssues(SampleIssues());
    CHECK(f.AllRows().find("NED-2") == std::string::npos);

    f.Press(test::ArrowRight());
    CHECK(f.AllRows().find("NED-2") != std::string::npos);
}

TEST_CASE("Issue rows open, open in the browser and copy", "[TrackerPanel]") {
    Fixture f;
    f.panel.NotifyShown();
    f.panel.ShowIssues(SampleIssues());

    f.Down(1);
    f.Press(test::Return());
    CHECK(f.opened == std::vector<std::string>{"NED-1"});
    f.Press(test::Character('o'));
    CHECK(f.urls == std::vector<std::string>{"https://x/NED-1"});
    f.Press(test::Character('w'));
    f.Press(test::Character('W'));
    CHECK(f.copied == std::vector<std::string>{"NED-1", "https://x/NED-1"});

    f.Down(3); // NED-2, which has no URL
    f.Press(test::Character('o'));
    f.Press(test::Character('W'));
    CHECK(f.urls.size() == 1);
    CHECK(f.copied.size() == 2);
    CHECK(f.messages.back() == "No URL for this row");

    // Enter on a header toggles it rather than opening anything.
    f.Press(test::ArrowUp());
    f.Press(test::Return());
    CHECK(f.opened.size() == 1);
    CHECK(f.AllRows().find("NED-2") == std::string::npos);

    f.Press(test::Escape());
    CHECK(f.cancels == 1);
}

TEST_CASE("A failed fetch keeps the last good listing", "[TrackerPanel]") {
    Fixture f;
    f.panel.NotifyShown();
    f.panel.ShowError("tracker panel \"Mine\": fetch failed (exit 4): gh: not logged in");
    CHECK(f.AllRows().find("Fetch failed") != std::string::npos);
    CHECK(f.messages.back().find("not logged in") != std::string::npos);

    // Enter on the failure row retries.
    f.Press(test::Return());
    CHECK(f.fetches == 2);
    f.panel.ShowIssues(SampleIssues());

    f.Press(test::Character('g'));
    f.panel.ShowError("offline");
    const std::string rows = f.AllRows();
    CHECK(rows.find("NED-1") != std::string::npos);
    CHECK(rows.find("Fetch failed") == std::string::npos);
}
