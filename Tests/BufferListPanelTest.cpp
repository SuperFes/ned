#include <catch2/catch_test_macros.hpp>

#include "TestEvents.h"
#include "Text/BufferList.h"
#include "UI/BufferListPanel.h"
#include "UI/Theme.h"

namespace {

struct Fixture {
    ned::ui::Theme       theme = ned::ui::DarkTheme();
    ned::text::BufferList bufferList;
};

} // namespace

TEST_CASE("BufferListPanel lists every open buffer and takes focus", "[BufferListPanel]") {
    Fixture fixture;
    fixture.bufferList.CreateBuffer("one");
    fixture.bufferList.CreateBuffer("two");

    ned::ui::BufferListPanel panel(fixture.theme, fixture.bufferList);
    panel.Show();
    REQUIRE(panel.Popup().Focusable());
    panel.Popup().TakeFocus();
    REQUIRE(panel.Popup().Focused());
}

TEST_CASE("BufferListPanel Enter switches to the selected buffer", "[BufferListPanel]") {
    Fixture fixture;
    ned::text::Buffer& one = fixture.bufferList.CreateBuffer("one");
    fixture.bufferList.CreateBuffer("two");

    ned::ui::BufferListPanel panel(fixture.theme, fixture.bufferList);
    ned::text::Buffer*       switched = nullptr;
    panel.SetOnRequestSwitchToBuffer([&](ned::text::Buffer& buffer) { switched = &buffer; });
    panel.Show();
    panel.Popup().TakeFocus();

    REQUIRE(panel.Popup().OnEvent(ned::ui::test::Return()));
    REQUIRE(switched == &one);
}

TEST_CASE("BufferListPanel digit key jumps straight to that buffer", "[BufferListPanel]") {
    Fixture fixture;
    fixture.bufferList.CreateBuffer("one");
    ned::text::Buffer& two = fixture.bufferList.CreateBuffer("two");

    ned::ui::BufferListPanel panel(fixture.theme, fixture.bufferList);
    ned::text::Buffer*       switched = nullptr;
    panel.SetOnRequestSwitchToBuffer([&](ned::text::Buffer& buffer) { switched = &buffer; });
    panel.Show();
    panel.Popup().TakeFocus();

    REQUIRE(panel.Popup().OnEvent(ned::ui::test::Character('2')));
    REQUIRE(switched == &two);
}

TEST_CASE("BufferListPanel Escape cancels without a kill confirmation pending", "[BufferListPanel]") {
    Fixture fixture;
    fixture.bufferList.CreateBuffer("one");

    ned::ui::BufferListPanel panel(fixture.theme, fixture.bufferList);
    bool                      cancelled = false;
    panel.SetOnCancel([&] { cancelled = true; });
    panel.Show();
    panel.Popup().TakeFocus();

    REQUIRE(panel.Popup().OnEvent(ned::ui::test::Escape()));
    REQUIRE(cancelled);
}

TEST_CASE("BufferListPanel marks a clean buffer and kills it on x with no confirmation", "[BufferListPanel]") {
    Fixture fixture;
    fixture.bufferList.CreateBuffer("one");
    fixture.bufferList.CreateBuffer("two");

    ned::ui::BufferListPanel panel(fixture.theme, fixture.bufferList);
    std::vector<std::string>  closingNames;
    panel.SetOnBufferClosing([&](ned::text::Buffer& buffer) { closingNames.push_back(buffer.Name()); });
    panel.Show();
    panel.Popup().TakeFocus();

    REQUIRE(panel.Popup().OnEvent(ned::ui::test::Character('d'))); // mark "one"
    REQUIRE(panel.Popup().OnEvent(ned::ui::test::Character('x'))); // execute

    REQUIRE(closingNames == std::vector<std::string>{"one"});
    REQUIRE(fixture.bufferList.Find("one") == nullptr);
    REQUIRE(fixture.bufferList.Find("two") != nullptr);
}

TEST_CASE("BufferListPanel requires y/n confirmation before killing a modified buffer, and n cancels it",
          "[BufferListPanel]") {
    Fixture             fixture;
    ned::text::Buffer& one = fixture.bufferList.CreateBuffer("one");
    one.InsertAtPoint("hello");
    REQUIRE(one.Modified());

    ned::ui::BufferListPanel panel(fixture.theme, fixture.bufferList);
    bool                      closed = false;
    panel.SetOnBufferClosing([&](ned::text::Buffer&) { closed = true; });
    panel.Show();
    panel.Popup().TakeFocus();

    REQUIRE(panel.Popup().OnEvent(ned::ui::test::Character('d')));
    REQUIRE(panel.Popup().OnEvent(ned::ui::test::Character('x')));
    REQUIRE_FALSE(closed); // confirmation pending, not executed yet

    REQUIRE(panel.Popup().OnEvent(ned::ui::test::Character('n')));
    REQUIRE_FALSE(closed);
    REQUIRE(fixture.bufferList.Find("one") != nullptr);

    REQUIRE(panel.Popup().OnEvent(ned::ui::test::Character('d')));
    REQUIRE(panel.Popup().OnEvent(ned::ui::test::Character('x')));
    REQUIRE(panel.Popup().OnEvent(ned::ui::test::Character('y')));
    REQUIRE(closed);
    REQUIRE(fixture.bufferList.Find("one") == nullptr);
}

TEST_CASE("BufferListPanel g refreshes rows and preserves marks by buffer identity", "[BufferListPanel]") {
    Fixture fixture;
    fixture.bufferList.CreateBuffer("one");

    ned::ui::BufferListPanel panel(fixture.theme, fixture.bufferList);
    panel.Show();
    panel.Popup().TakeFocus();

    REQUIRE(panel.Popup().OnEvent(ned::ui::test::Character('d'))); // mark "one"
    fixture.bufferList.CreateBuffer("two");
    REQUIRE(panel.Popup().OnEvent(ned::ui::test::Character('g'))); // refresh -- "two" now also listed

    std::vector<std::string> closingNames;
    panel.SetOnBufferClosing([&](ned::text::Buffer& buffer) { closingNames.push_back(buffer.Name()); });
    REQUIRE(panel.Popup().OnEvent(ned::ui::test::Character('x')));
    REQUIRE(closingNames == std::vector<std::string>{"one"}); // only the mark from before refresh
}
