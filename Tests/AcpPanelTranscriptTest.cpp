//
// AcpPanel/ -- the scroll model and transcript formatter, without a panel.
//

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "UI/AcpPanel/ChoicePicker.h"
#include "UI/AcpPanel/ComposerLayout.h"
#include "UI/AcpPanel/TranscriptFormat.h"
#include "UI/AcpPanel/TranscriptScroll.h"

using ned::editor::acp::Manager;
using ned::ui::acppanel::FormatTranscript;
using ned::ui::acppanel::TranscriptScroll;
using ned::ui::acppanel::WrapDisplayLines;
using Kind = Manager::TranscriptEntry::Kind;

TEST_CASE("TranscriptScroll follows the tail until scrolled away", "[AcpPanel][AcpScroll]") {
    TranscriptScroll scroll;
    REQUIRE(scroll.FirstVisibleRow(3, 5) == 0);
    REQUIRE(scroll.FirstVisibleRow(20, 5) == 15);
    REQUIRE(scroll.FirstVisibleRow(21, 5) == 16);
    REQUIRE(scroll.Following());
}

TEST_CASE("TranscriptScroll keeps a scrolled-back view still while rows are appended", "[AcpPanel][AcpScroll]") {
    TranscriptScroll scroll;
    scroll.ScrollBy(-4, 20, 5);
    REQUIRE_FALSE(scroll.Following());
    REQUIRE(scroll.FirstVisibleRow(20, 5) == 11);
    REQUIRE(scroll.FirstVisibleRow(40, 5) == 11);
}

TEST_CASE("TranscriptScroll resumes following once scrolled back to the bottom", "[AcpPanel][AcpScroll]") {
    TranscriptScroll scroll;
    scroll.ScrollBy(-4, 20, 5);
    scroll.ScrollBy(100, 20, 5);
    REQUIRE(scroll.Following());
    REQUIRE(scroll.FirstVisibleRow(30, 5) == 25);
}

TEST_CASE("TranscriptScroll clamps against a transcript that shrank under it", "[AcpPanel][AcpScroll]") {
    TranscriptScroll scroll;
    scroll.ScrollToRow(30, 50, 5);
    REQUIRE(scroll.FirstVisibleRow(50, 5) == 30);
    REQUIRE(scroll.FirstVisibleRow(10, 5) == 5);
    REQUIRE(scroll.Following());
}

TEST_CASE("TranscriptScroll::ScrollToTop pins row 0 even as rows arrive", "[AcpPanel][AcpScroll]") {
    TranscriptScroll scroll;
    scroll.ScrollToTop();
    REQUIRE(scroll.FirstVisibleRow(50, 5) == 0);
    REQUIRE(scroll.FirstVisibleRow(80, 5) == 0);
    scroll.FollowTail();
    REQUIRE(scroll.FirstVisibleRow(80, 5) == 75);
}

TEST_CASE("FormatTranscript tags every line with the entry it renders", "[AcpPanel]") {
    std::vector<Manager::TranscriptEntry> transcript;
    transcript.push_back({.kind = Kind::UserMessage, .text = "hi"});
    transcript.push_back({.kind = Kind::AgentText, .text = "one\ntwo"});
    const auto lines = FormatTranscript(transcript, std::nullopt, {.width = 40});
    REQUIRE(lines.size() == 3);
    REQUIRE(lines[0].entryIndex == 0);
    REQUIRE(lines[1].entryIndex == 1);
    REQUIRE(lines[2].entryIndex == 1);
}

TEST_CASE("WrapDisplayLines maps each wrapped row back to its logical line", "[AcpPanel]") {
    std::vector<ned::ui::acppanel::DisplayLine> lines;
    lines.push_back({.text = "aaaa bbbb cccc"});
    lines.push_back({.text = "dd"});
    const auto rows = WrapDisplayLines(lines, 5);
    REQUIRE(rows.size() == 4);
    REQUIRE(rows[0].lineIndex == 0);
    REQUIRE(rows[2].lineIndex == 0);
    REQUIRE(rows[3].lineIndex == 1);
    REQUIRE(rows[3].text == "dd");
}

namespace {

ned::editor::KeyChord Key(char32_t codepoint) {
    return {.Codepoint = codepoint};
}

ned::editor::KeyChord Special(ned::editor::SpecialKey key) {
    return {.Special = key};
}

} // namespace

TEST_CASE("ChoicePicker picks by digit, by Enter on the selection, and closes on Escape", "[AcpPanel]") {
    using ned::ui::acppanel::ChoicePicker;
    std::vector<std::size_t> chosen;
    auto                     make = [&chosen] {
        return ChoicePicker("Pick", {{.label = "alpha"}, {.label = "beta", .current = true}, {.label = "gamma"}},
                            [&chosen](std::size_t index) { chosen.push_back(index); });
    };

    ChoicePicker byDigit = make();
    REQUIRE(byDigit.HandleKey(Key(U'3')) == ChoicePicker::KeyResult::Chosen);
    REQUIRE(chosen.back() == 2);

    ChoicePicker byEnter = make();
    REQUIRE(byEnter.Selection() == 1); // starts on the current value
    byEnter.HandleKey(Special(ned::editor::SpecialKey::Down));
    REQUIRE(byEnter.HandleKey(Special(ned::editor::SpecialKey::Enter)) == ChoicePicker::KeyResult::Chosen);
    REQUIRE(chosen.back() == 2);

    ChoicePicker closed = make();
    REQUIRE(closed.HandleKey(Special(ned::editor::SpecialKey::Escape)) == ChoicePicker::KeyResult::Closed);
    REQUIRE(chosen.size() == 2);
}

TEST_CASE("ChoicePicker narrows by typed text and chooses among what's left", "[AcpPanel]") {
    using ned::ui::acppanel::ChoicePicker;
    std::size_t  chosen = 99;
    ChoicePicker picker("Pick", {{.label = "Opus"}, {.label = "Sonnet"}, {.label = "Haiku"}}, [&chosen](std::size_t index) { chosen = index; });
    picker.HandleKey(Key(U'h'));
    picker.HandleKey(Key(U'k'));
    REQUIRE(picker.Visible() == std::vector<std::size_t>{2});
    const auto lines = picker.Format(10);
    REQUIRE(lines[0].text == "Pick  hk");
    REQUIRE(lines[1].text.find("Haiku") != std::string::npos);
    REQUIRE(picker.HandleKey(Special(ned::editor::SpecialKey::Enter)) == ChoicePicker::KeyResult::Chosen);
    REQUIRE(chosen == 2);
}

TEST_CASE("LayoutComposer splits at newlines, wraps each line, and places the caret by byte", "[AcpPanel]") {
    using ned::ui::acppanel::LayoutComposer;
    const std::string text   = "ab\ncd ef gh";
    const auto        layout = LayoutComposer(text, 7, 5); // after "cd e"
    REQUIRE(layout.rows.size() == 3);
    REQUIRE(layout.rows[0].text == "ab");
    REQUIRE(layout.rows[1].text == "cd ");
    REQUIRE(layout.rows[1].byteStart == 3);
    REQUIRE(layout.rows[2].text == "ef gh");
    REQUIRE(layout.rows[2].byteStart == 6);
    REQUIRE(layout.caretRow == 2);
    REQUIRE(layout.caretColumn == 1);

    const auto atNewline = LayoutComposer(text, 2, 5);
    REQUIRE(atNewline.caretRow == 0);
    REQUIRE(atNewline.caretColumn == 2);

    const auto trailing = LayoutComposer("ab\n", 3, 5);
    REQUIRE(trailing.rows.size() == 2);
    REQUIRE(trailing.caretRow == 1);
    REQUIRE(trailing.caretColumn == 0);
}

TEST_CASE("ByteAtColumn clamps to the row's end", "[AcpPanel]") {
    using ned::ui::acppanel::ByteAtColumn;
    using ned::ui::acppanel::LayoutComposer;
    const std::string text   = "abcdef\nxy";
    const auto        layout = LayoutComposer(text, 0, 20);
    REQUIRE(ByteAtColumn(text, layout.rows[0], 3) == 3);
    REQUIRE(ByteAtColumn(text, layout.rows[1], 5) == 9);
}

TEST_CASE("FormatTranscript tints fenced code as code, drops the fences, and leaves its markup alone", "[AcpPanel]") {
    std::vector<Manager::TranscriptEntry> transcript;
    transcript.push_back({.kind = Kind::AgentText, .text = "Run:\n```sh\nls **/*.cpp\n```\ndone"});
    const auto lines = FormatTranscript(transcript, std::nullopt, {.width = 40});
    REQUIRE(lines.size() == 3);
    REQUIRE(lines[0].text == "Run:");
    REQUIRE(lines[1].text == "ls **/*.cpp");
    REQUIRE(lines[1].spans.size() == 1);
    REQUIRE(lines[1].spans[0].code);
    REQUIRE(lines[1].spans[0].columnCount == 11);
    REQUIRE(lines[2].text == "done");
}

TEST_CASE("FormatTranscript shows only the first line of a multi-line tool title", "[AcpPanel]") {
    std::vector<Manager::TranscriptEntry> transcript;
    transcript.push_back({.kind = Kind::ToolCall, .text = "git add a && \\\ngit commit", .status = "completed", .toolKind = "execute"});
    const auto lines = FormatTranscript(transcript, std::nullopt, {.width = 60});
    REQUIRE(lines[0].text.find("▸ $ git add a && \\ …") == 0);
    REQUIRE(lines[0].text.find('\n') == std::string::npos);
}

TEST_CASE("FormatTranscript marks a steered message apart from a prompt", "[AcpPanel]") {
    std::vector<Manager::TranscriptEntry> transcript;
    transcript.push_back({.kind = Kind::UserMessage, .text = "go"});
    transcript.push_back({.kind = Kind::UserMessage, .text = "faster", .status = "steered"});
    const auto lines = FormatTranscript(transcript, std::nullopt, {.width = 40});
    REQUIRE(lines[0].text == "> go");
    REQUIRE(lines[1].text == "↳ faster");
}

TEST_CASE("FormatTranscript lays a multi-line prompt out one line per row", "[AcpPanel]") {
    std::vector<Manager::TranscriptEntry> transcript;
    transcript.push_back({.kind = Kind::UserMessage, .text = "first\nsecond"});
    const auto lines = FormatTranscript(transcript, std::nullopt, {.width = 40});
    REQUIRE(lines.size() == 2);
    REQUIRE(lines[0].text == "> first");
    REQUIRE(lines[1].text == "  second");
    REQUIRE(lines[1].entryIndex == 0);
}
