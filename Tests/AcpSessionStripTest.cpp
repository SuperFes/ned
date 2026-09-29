#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "Text/DisplayWidth.h"
#include "UI/AcpPanel/SessionStrip.h"

using ned::editor::acp::Manager;
using ned::ui::acppanel::LayoutSessionStrip;
using ned::ui::acppanel::SessionStripItem;
using ned::ui::acppanel::SessionStripItemAt;

namespace {

std::vector<Manager::SessionTab> Tabs(std::size_t count, std::size_t current) {
    std::vector<Manager::SessionTab> tabs;
    for (std::size_t i = 0; i < count; ++i) {
        tabs.push_back({.key = i + 1, .label = "conversation " + std::to_string(i + 1), .current = i == current, .live = true});
    }
    return tabs;
}

std::string Joined(const std::vector<SessionStripItem>& items) {
    std::string text;
    for (const SessionStripItem& item : items) {
        text += item.text;
    }
    return text;
}

} // namespace

TEST_CASE("LayoutSessionStrip numbers tabs, closes the current one and ends with +", "[AcpPanel]") {
    std::vector<Manager::SessionTab> tabs     = Tabs(2, 1);
    tabs[0].attention                         = true;
    const std::vector<SessionStripItem> items = LayoutSessionStrip(tabs, 80);
    REQUIRE(Joined(items) == " 1 ● conversation 1  2 conversation 2  ×  + ");
    REQUIRE(items[2].kind == SessionStripItem::Kind::Close);
    REQUIRE(items[2].key == 2);
    REQUIRE(items.back().kind == SessionStripItem::Kind::New);

    int x = 0;
    for (const SessionStripItem& item : items) {
        REQUIRE(item.x == x);
        x += item.columns;
    }
    REQUIRE(SessionStripItemAt(items, items[1].x)->key == 2);
    REQUIRE(SessionStripItemAt(items, x) == nullptr);
}

TEST_CASE("LayoutSessionStrip shortens labels, then scrolls, keeping the current tab", "[AcpPanel]") {
    const std::vector<SessionStripItem> shortened = LayoutSessionStrip(Tabs(2, 0), 30);
    REQUIRE(ned::text::StringColumns(Joined(shortened)) <= 30);
    REQUIRE(Joined(shortened).find("…") != std::string::npos);

    const std::vector<SessionStripItem> scrolled = LayoutSessionStrip(Tabs(12, 9), 24);
    REQUIRE(ned::text::StringColumns(Joined(scrolled)) <= 24);
    REQUIRE(scrolled.front().kind == SessionStripItem::Kind::More);
    const auto current = std::find_if(scrolled.begin(), scrolled.end(), [](const SessionStripItem& item) {
        return item.kind == SessionStripItem::Kind::Tab && item.current;
    });
    REQUIRE(current != scrolled.end());
    REQUIRE(current->text == " 10 ");
}

TEST_CASE("LayoutSessionStrip offers no close button for a lone tab", "[AcpPanel]") {
    const std::vector<SessionStripItem> items = LayoutSessionStrip(Tabs(1, 0), 80);
    REQUIRE(items.size() == 2);
    REQUIRE(items.front().kind == SessionStripItem::Kind::Tab);
    REQUIRE(items.back().kind == SessionStripItem::Kind::New);
}
