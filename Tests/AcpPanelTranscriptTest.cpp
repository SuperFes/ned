//
// AcpPanel/ -- the scroll model and transcript formatter, without a panel.
//

#include <catch2/catch_test_macros.hpp>

#include <chrono>
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
    REQUIRE(lines.size() == 4);
    REQUIRE(lines[0].text == "Run:");
    REQUIRE(lines[1].text.starts_with("sh "));
    REQUIRE(lines[1].text.ends_with("⧉ copy"));
    REQUIRE(lines[1].action == ned::ui::acppanel::LineAction::Copy);
    REQUIRE(lines[1].copyText == "ls **/*.cpp");
    REQUIRE(lines[2].text == "ls **/*.cpp");
    REQUIRE(lines[2].spans.size() == 1);
    REQUIRE(lines[2].spans[0].code);
    REQUIRE(lines[2].spans[0].columnCount == 11);
    REQUIRE(lines[3].text == "done");
}

TEST_CASE("FormatTranscript colours fenced code through the fence's language, line by line", "[AcpPanel]") {
    using ned::editor::HighlightSpan;
    using ned::editor::SyntaxClass;
    std::vector<Manager::TranscriptEntry> transcript;
    transcript.push_back({.kind = Kind::AgentText, .text = "```cpp\nint x;\n\treturn;\n```"});
    std::string seenLanguage;
    std::string seenCode;
    const auto  lines = FormatTranscript(transcript, std::nullopt,
                                         {.width         = 40,
                                          .highlightCode = [&](std::string_view language, std::string_view code) {
                                             seenLanguage = language;
                                             seenCode     = code;
                                             // "int" and "return", the second spanning the tab before it
                                             return std::vector<HighlightSpan>{{.startByte = 0, .endByte = 3, .syntaxClass = SyntaxClass::Type},
                                                                               {.startByte = 7, .endByte = 14, .syntaxClass = SyntaxClass::Keyword}};
                                          }});
    REQUIRE(seenLanguage == "cpp");
    REQUIRE(seenCode == "int x;\n\treturn;");
    REQUIRE(lines.size() == 3);

    const auto& first = lines[1];
    REQUIRE(first.text == "int x;");
    REQUIRE(first.spans.size() == 2);
    REQUIRE(first.spans[0].columnCount == 3);
    REQUIRE(first.spans[0].syntaxClass == SyntaxClass::Type);
    REQUIRE_FALSE(first.spans[1].syntaxClass.has_value());
    REQUIRE(first.spans[1].code);

    const auto& second = lines[2];
    REQUIRE(second.text == "    return;"); // the tab expands to the next 4-column stop
    REQUIRE(second.spans[0].startColumn == 0);
    REQUIRE(second.spans[0].columnCount == 10);
    REQUIRE(second.spans[0].syntaxClass == SyntaxClass::Keyword);
}

TEST_CASE("ExtractCodeBlocks returns each fenced block, an unclosed last one included", "[AcpPanel]") {
    using ned::ui::acppanel::ExtractCodeBlocks;
    const auto blocks = ExtractCodeBlocks("a\n```python\nprint(1)\n```\nb\n``` {.rust}\nfn main() {}\nlet x = 1;");
    REQUIRE(blocks.size() == 2);
    REQUIRE(blocks[0].language == "python");
    REQUIRE(blocks[0].code == "print(1)");
    REQUIRE(blocks[1].language == "rust");
    REQUIRE(blocks[1].code == "fn main() {}\nlet x = 1;");
}

TEST_CASE("FormatTranscript aligns a Markdown table's columns and bolds its header", "[AcpPanel]") {
    std::vector<Manager::TranscriptEntry> transcript;
    transcript.push_back({.kind = Kind::AgentText, .text = "| Name | Count |\n|:---|---:|\n| `a\\|b` | 7 |\n| longer | 12 |\nafter"});
    const auto lines = FormatTranscript(transcript, std::nullopt, {.width = 40});
    REQUIRE(lines.size() == 5);
    REQUIRE(lines[0].text == "Name   │ Count");
    REQUIRE(lines[1].text == "───────┼──────");
    REQUIRE(lines[2].text == "a|b    │     7");
    REQUIRE(lines[3].text == "longer │    12");
    REQUIRE(lines[4].text == "after");
    REQUIRE(lines[0].spans.size() == 2);
    REQUIRE(lines[0].spans[0].bold);
    REQUIRE(lines[0].spans[1].bold);
    REQUIRE(lines[2].spans.size() == 1);
    REQUIRE(lines[2].spans[0].code);
    REQUIRE(lines[2].spans[0].columnCount == 3);
}

TEST_CASE("FormatTranscript leaves a table too wide for the panel as its own text", "[AcpPanel]") {
    std::vector<Manager::TranscriptEntry> transcript;
    transcript.push_back({.kind = Kind::AgentText, .text = "| a very long header | another |\n|---|---|\n| x | y |"});
    const auto lines = FormatTranscript(transcript, std::nullopt, {.width = 20});
    REQUIRE(lines.size() == 3);
    REQUIRE(lines[0].text == "| a very long header | another |");
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

TEST_CASE("FormatTranscript counts up a running tool call and tails a running command", "[AcpPanel]") {
    const auto                            now = std::chrono::steady_clock::now();
    std::vector<Manager::TranscriptEntry> transcript;
    Manager::TranscriptEntry              entry{.kind = Kind::ToolCall, .text = "make", .status = "in_progress"};
    entry.toolKind   = "execute";
    entry.terminal   = true;
    entry.toolOutput = "one\ntwo\nthree\nfour\n";
    entry.startedAt  = now - std::chrono::seconds(5);
    transcript.push_back(entry);
    const auto lines = FormatTranscript(transcript, std::nullopt, {.width = 40, .now = now});
    REQUIRE(lines.size() == 4);
    REQUIRE(lines[0].text.ends_with("… 5s"));
    REQUIRE(lines[1].text == "    two");
    REQUIRE(lines[3].text == "    four");
}

TEST_CASE("FormatTranscript shows a failed command's exit code and its output's end", "[AcpPanel]") {
    std::vector<Manager::TranscriptEntry> transcript;
    Manager::TranscriptEntry              entry{.kind = Kind::ToolCall, .text = "make", .status = "failed"};
    entry.terminal = true;
    entry.exitCode = 2;
    for (int i = 1; i <= 20; ++i) {
        entry.toolOutput += "line " + std::to_string(i) + "\n";
    }
    transcript.push_back(entry);
    const auto collapsed = FormatTranscript(transcript, std::nullopt, {.width = 40});
    REQUIRE(collapsed.size() == 1);
    REQUIRE(collapsed[0].text.ends_with("✗ exit 2"));

    const auto expanded = FormatTranscript(transcript, std::nullopt, {.width = 40, .expanded = [](std::size_t) { return true; }});
    REQUIRE(expanded[1].text == "    (8 earlier line(s))");
    REQUIRE(expanded[2].text == "    line 9");
    REQUIRE(expanded.back().text == "    line 20");
}

TEST_CASE("FormatTranscript keeps a long finished tool call's duration", "[AcpPanel]") {
    const auto                            now = std::chrono::steady_clock::now();
    std::vector<Manager::TranscriptEntry> transcript;
    Manager::TranscriptEntry              quick{.kind = Kind::ToolCall, .text = "ls", .status = "completed"};
    quick.startedAt  = now - std::chrono::seconds(3);
    quick.finishedAt = now - std::chrono::seconds(1);
    Manager::TranscriptEntry slow{.kind = Kind::ToolCall, .text = "build", .status = "completed"};
    slow.startedAt  = now - std::chrono::seconds(60);
    slow.finishedAt = now - std::chrono::seconds(18);
    transcript.push_back(quick);
    transcript.push_back(slow);
    const auto lines = FormatTranscript(transcript, std::nullopt, {.width = 40, .now = now});
    REQUIRE(lines[0].text.ends_with("✓"));
    REQUIRE(lines[1].text.ends_with("✓ 42s"));
}

TEST_CASE("CopyCandidates offers each recent reply, then its code blocks, newest first", "[AcpPanel]") {
    std::vector<Manager::TranscriptEntry> transcript;
    transcript.push_back({.kind = Kind::AgentText, .text = "old reply"});
    transcript.push_back({.kind = Kind::UserMessage, .text = "more"});
    transcript.push_back({.kind = Kind::AgentText, .text = "\nTry:\n```sh\nmake\n```\nor\n```py\nrun()\nagain()\n```\n"});
    const auto candidates = ned::ui::acppanel::CopyCandidates(transcript, 10);
    REQUIRE(candidates.size() == 4);
    REQUIRE(candidates[0].label == "Try:");
    REQUIRE(candidates[0].detail == "reply · 10 lines");
    REQUIRE(candidates[1].text == "run()\nagain()");
    REQUIRE(candidates[1].detail == "py · 2 lines");
    REQUIRE(candidates[2].text == "make");
    REQUIRE(candidates[3].text == "old reply");
    REQUIRE(ned::ui::acppanel::CopyCandidates(transcript, 1).size() == 3);
}

TEST_CASE("FormatTranscript styles a notice by severity and indents its description's later lines", "[AcpPanel]") {
    using ned::ui::acppanel::DisplayStyle;
    std::vector<Manager::TranscriptEntry> transcript;
    transcript.push_back({.kind = Kind::Notice, .text = "Task stopped", .status = "info"});
    transcript.push_back({.kind = Kind::Notice, .text = "Hook failed", .status = "error", .detail = "exit 2\nstderr: boom"});
    const auto lines = FormatTranscript(transcript, std::nullopt, {.width = 40});
    REQUIRE(lines.size() == 3);
    REQUIRE(lines[0].text == "ℹ Task stopped");
    REQUIRE(lines[0].style == DisplayStyle::Hint);
    REQUIRE(lines[1].text == "✗ Hook failed: exit 2");
    REQUIRE(lines[1].style == DisplayStyle::Error);
    REQUIRE(lines[2].text == "  stderr: boom");
    REQUIRE(lines[2].entryIndex == 1);
}

TEST_CASE("FormatTranscript shows a compaction's state and folds its summary", "[AcpPanel]") {
    using ned::ui::acppanel::LineAction;
    std::vector<Manager::TranscriptEntry> transcript;
    transcript.push_back({.kind = Kind::Compaction, .text = "Kept:\n- the plan", .status = "completed"});
    transcript.push_back({.kind = Kind::Compaction, .status = "in_progress"});
    transcript.push_back({.kind = Kind::Compaction, .status = "failed", .detail = "too big"});

    auto lines = FormatTranscript(transcript, std::nullopt, {.width = 60});
    REQUIRE(lines.size() == 3);
    REQUIRE(lines[0].text == "▸ Context compacted (2 lines of summary)");
    REQUIRE(lines[0].action == LineAction::ToggleExpand);
    REQUIRE(lines[1].text == "⟳ Compacting context…");
    REQUIRE(lines[2].text == "✗ Context compaction failed: too big");

    lines = FormatTranscript(transcript, std::nullopt, {.width = 60, .expanded = [](std::size_t i) { return i == 0; }});
    REQUIRE(lines[0].text == "▾ Context compacted");
    REQUIRE(lines[1].text == "  Kept:");
    REQUIRE(lines[1].entryIndex == 0);
}

TEST_CASE("FormatTranscript links an agent's resources and folds embedded ones", "[AcpPanel]") {
    using ned::ui::acppanel::LineAction;
    std::vector<Manager::TranscriptEntry> transcript;
    transcript.push_back({.kind = Kind::AgentContent, .status = "image", .mimeType = "image/png", .byteSize = 84 * 1024});
    transcript.push_back({.kind = Kind::AgentContent, .status = "resource_link", .detail = "file:///src/a%20b.cpp", .contentName = "a b.cpp"});
    transcript.push_back({.kind = Kind::AgentContent, .status = "resource_link", .detail = "https://x.test/doc", .contentName = "doc"});
    transcript.push_back({.kind = Kind::AgentContent, .text = "int x;\n", .status = "resource", .detail = "file:///m.cpp", .contentName = "m.cpp"});

    auto lines = FormatTranscript(transcript, std::nullopt, {.width = 60});
    REQUIRE(lines.size() == 4);
    REQUIRE(lines[0].text == "▣ image · image/png · 84 KB");
    REQUIRE(lines[0].action == LineAction::None);
    REQUIRE(lines[1].text == "↗ a b.cpp");
    REQUIRE(lines[1].action == LineAction::OpenLocation);
    REQUIRE(lines[1].location->path == "/src/a b.cpp");
    REQUIRE(lines[2].action == LineAction::OpenUrl);
    REQUIRE(lines[2].url == "https://x.test/doc");
    REQUIRE(lines[3].text == "▸ m.cpp (1 line)");

    lines = FormatTranscript(transcript, std::nullopt, {.width = 60, .expanded = [](std::size_t i) { return i == 3; }});
    REQUIRE(lines[3].text == "▾ m.cpp");
    REQUIRE(lines[4].text.starts_with("  cpp"));
    REQUIRE(lines[5].text == "  int x;");
}

TEST_CASE("ChoicePicker deletes the selection only after a y, and only when told it went", "[AcpPanel]") {
    using ned::ui::acppanel::ChoiceItem;
    using ned::ui::acppanel::ChoicePicker;
    using KeyResult                 = ChoicePicker::KeyResult;
    auto                        key = [](char32_t c, bool control = false) { return ned::editor::KeyChord{.Control = control, .Codepoint = c}; };
    const ned::editor::KeyChord del{.Special = ned::editor::SpecialKey::Delete};

    ChoicePicker plain("Pick", {ChoiceItem{.label = "a"}}, nullptr);
    REQUIRE(plain.HandleKey(del) == KeyResult::Handled);
    REQUIRE(plain.Format(5)[0].text == "Pick");

    std::vector<std::size_t> deleted;
    ChoicePicker             picker("Pick", {ChoiceItem{.label = "a"}, ChoiceItem{.label = "b"}, ChoiceItem{.label = "c"}}, nullptr);
    picker.SetOnDelete([&deleted](std::size_t index) {
        deleted.push_back(index);
        return index != 0;
    });
    // Refused: stays.
    picker.HandleKey(del);
    picker.HandleKey(key(U'y'));
    REQUIRE(picker.Visible().size() == 3);
    // Not confirmed: stays, and the key isn't typed into the filter.
    picker.HandleKey(key(U'n', true));
    picker.HandleKey(key(U'd', true));
    REQUIRE(picker.Format(5)[0].text == "Delete \"b\"? (y/n)");
    picker.HandleKey(key(U'x'));
    REQUIRE(picker.Format(5)[0].text == "Pick");
    // Confirmed: gone, selection clamped.
    picker.HandleKey(key(U'n', true));
    picker.HandleKey(del);
    REQUIRE(picker.HandleKey(key(U'y')) == KeyResult::Handled);
    REQUIRE(deleted == std::vector<std::size_t>{0, 2});
    REQUIRE(picker.Visible() == std::vector<std::size_t>{0, 1});
    REQUIRE(picker.Selection() == 1);
}

TEST_CASE("FormatTranscript gives each picture it can fit rows of its own", "[AcpPanel]") {
    std::vector<Manager::TranscriptEntry> transcript;
    transcript.push_back({.kind = Kind::UserMessage, .text = "look\n\n[attached: image]", .images = {{.id = 7, .data = "x"}}});
    transcript.push_back({.kind = Kind::AgentContent, .status = "image", .mimeType = "image/png", .images = {{.id = 8, .data = "y"}, {.id = 9, .data = "z"}}});

    // Without a fit, only the captions.
    auto lines = FormatTranscript(transcript, std::nullopt, {.width = 40});
    REQUIRE(lines.size() == 4);

    std::vector<int> maxColumnsAsked;
    lines = FormatTranscript(transcript, std::nullopt,
                             {.width    = 40,
                              .imageFit = [&maxColumnsAsked](const Manager::TranscriptImage& image, int maxColumns) -> std::optional<ned::editor::image::CellFit> {
                                  maxColumnsAsked.push_back(maxColumns);
                                  if (image.id == 9) {
                                      return std::nullopt; // doesn't decode
                                  }
                                  return ned::editor::image::CellFit{.columns = 5, .rows = 2};
                              }});
    REQUIRE(maxColumnsAsked == std::vector<int>{38, 38, 38});
    REQUIRE(lines.size() == 8);
    REQUIRE(lines[3].image);
    REQUIRE(lines[3].image->id == 7);
    REQUIRE(lines[3].image->row == 0);
    REQUIRE(lines[4].image->row == 1);
    REQUIRE(lines[4].image->column == 2);
    REQUIRE(lines[4].entryIndex == 0);
    REQUIRE(lines[5].text == "▣ image · image/png");
    REQUIRE(lines[6].image->id == 8);
    REQUIRE(lines[7].image->rows == 2);
    REQUIRE(lines[7].entryIndex == 1);
}
