//
// AcpPanel (Source/UI/AcpPanel.h) -- headless coverage over a real
// Manager wired to a pipe-backed Client, the same ManagerFixture/
// DispatchFrame pattern AcpManagerTest.cpp uses, plus TerminalPanelTest's
// own per-cell Screen::PixelAt painting convention.
//

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <thread>

#include <poll.h>
#include <unistd.h>

#include "Editor/Acp/Client.h"
#include "Editor/Acp/Config.h"
#include "Editor/Acp/Manager.h"
#include "Editor/Acp/PanelConfig.h"
#include "Editor/Acp/Transport.h"
#include "Editor/Lsp/Client.h"
#include "Editor/Lsp/Manager.h"
#include "Editor/Lsp/ServerConfig.h"
#include "Editor/Lsp/Transport.h"
#include "Editor/Project/Root.h"
#include "TestEvents.h"
#include "Text/Base64.h"
#include "Text/BufferList.h"
#include "UI/AcpPanel.h"
#include "UI/EventLoop.h"
#include "UI/Widget.h"

namespace {

using ned::editor::acp::Json;
using ned::editor::acp::Transport;
using ned::editor::lsp::kProseLanguageKey;
using ned::ui::AcpPanel;
using ned::ui::Box;
using ned::ui::Canvas;
using ned::ui::Screen;
using ned::ui::Theme;

constexpr int kWidth  = 40;
constexpr int kHeight = 6; // 1 title + 4 content + 1 input

// Mirrors AcpManagerTest.cpp's own MessageReader/ManagerFixture exactly --
// duplicated rather than shared across Tests/ files, matching this
// codebase's existing per-test-file fixture convention (no shared Tests/
// support header for this shape).
struct MessageReader {
    int         fd;
    std::string buffer;

    Json Next() {
        for (int i = 0; i < 16; ++i) {
            const auto newlinePos = buffer.find('\n');
            if (newlinePos != std::string::npos) {
                const std::string line = buffer.substr(0, newlinePos);
                buffer.erase(0, newlinePos + 1);
                return Json::parse(line);
            }
            char          chunk[512];
            const ssize_t n = ::read(fd, chunk, sizeof(chunk));
            if (n <= 0) {
                break;
            }
            buffer.append(chunk, static_cast<std::size_t>(n));
        }
        FAIL("no complete message available on fd");
        return Json::object();
    }
};

std::string ResultFrame(const Json& id, const Json& result) {
    return Json{{"jsonrpc", "2.0"}, {"id", id}, {"result", result}}.dump();
}

struct Fixture {
    ned::ui::EventLoop    eventLoop;
    ned::text::BufferList bufferList;
    ned::editor::acp::Manager manager{bufferList, eventLoop};
    ned::editor::lsp::Manager lspManager{bufferList, eventLoop};
    Theme                 theme = ned::ui::DarkTheme();
    AcpPanel              panel{theme};
    Screen                screen{kWidth, kHeight};

    int           agentStdinRead   = -1;
    int           agentStdoutWrite = -1;
    ned::editor::acp::Client* client           = nullptr;
    MessageReader reader{-1};

    Fixture() {
        panel.SetAcpManager(&manager);
        panel.SetLspManager(&lspManager);
        panel.SetBox_(Box{.x_min = 0, .x_max = kWidth - 1, .y_min = 0, .y_max = kHeight - 1});
    }

    void InjectClient() {
        int clientWritesHere[2];
        int clientReadsHere[2];
        REQUIRE(::pipe(clientWritesHere) == 0);
        REQUIRE(::pipe(clientReadsHere) == 0);
        agentStdinRead   = clientWritesHere[0];
        agentStdoutWrite = clientReadsHere[1];
        reader.fd        = agentStdinRead;
        client           = &manager.SetClientForTesting(
            std::make_unique<ned::editor::acp::Client>(Transport(clientReadsHere[0], clientWritesHere[1]), eventLoop));
    }

    void StartActiveSession(const std::string& agentName, Json sessionNewResult = Json::object(), const Json& initializeResult = Json::object()) {
        manager.StartSession(agentName);
        const Json initializeRequest = reader.Next();
        client->DispatchFrame(ResultFrame(initializeRequest["id"], initializeResult));
        const Json sessionNewRequest  = reader.Next();
        sessionNewResult["sessionId"] = "s1";
        client->DispatchFrame(ResultFrame(sessionNewRequest["id"], sessionNewResult));
        REQUIRE(manager.State() == ned::editor::acp::Manager::SessionState::Active);
    }

    void Paint() {
        panel.Paint(Canvas(screen, panel.Box_()));
    }

    void SendUpdate(const Json& update) {
        client->DispatchFrame(
            Json{{"jsonrpc", "2.0"}, {"method", "session/update"}, {"params", {{"sessionId", "s1"}, {"update", update}}}}.dump());
    }

    void AgentSays(const std::string& text) {
        SendUpdate({{"sessionUpdate", "agent_message_chunk"}, {"content", {{"type", "text"}, {"text", text}}}});
    }

    // Every content row between the title and the input row, joined by '\n'.
    [[nodiscard]] std::string ContentText() {
        std::string joined;
        for (int y = 1; y < kHeight - 1; ++y) {
            joined += RowText(y) + "\n";
        }
        return joined;
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

    ~Fixture() {
        if (agentStdoutWrite >= 0) {
            ::close(agentStdoutWrite);
        }
        if (agentStdinRead >= 0) {
            ::close(agentStdinRead);
        }
    }
};

// @-file-mention autocomplete follow-up: ProjectRoot is process-wide state --
// BufferViewProjectFindReferencesTest.cpp's own ProjectRootGuard, mirrored
// here rather than shared (this codebase's existing per-test-file fixture
// convention, see MessageReader's own comment above).
struct ProjectRootGuard {
    std::filesystem::path previous = ned::editor::ProjectRoot();
    ~ProjectRootGuard() {
        ned::editor::SetProjectRoot(previous);
    }
};

// Prose-check-the-composer follow-up. ManagerTest.cpp's own WaitUntil,
// duplicated per this file's stated per-test-file fixture convention (see
// MessageReader's own comment above) -- polls eventLoop's posted-work queue
// until predicate is true, since CheckComposerProseText's debounce timer
// fires on a background thread and Posts its work back onto eventLoop.
template <typename Predicate>
void WaitUntil(ned::ui::EventLoop& eventLoop, Predicate predicate) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!predicate() && std::chrono::steady_clock::now() < deadline) {
        eventLoop.DrainPosted_();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

// ManagerTest.cpp's own ReadRawFrame, duplicated here (same rationale).
std::string ReadRawFrame(int fd) {
    std::string all;
    char        buffer[512];
    for (int i = 0; i < 4; ++i) {
        const ssize_t n = ::read(fd, buffer, sizeof(buffer));
        if (n <= 0) {
            break;
        }
        all.append(buffer, static_cast<std::size_t>(n));
        const auto headerEnd = all.find("\r\n\r\n");
        if (headerEnd != std::string::npos) {
            const std::string_view kPrefix   = "Content-Length: ";
            const auto             prefixPos = all.find(kPrefix);
            if (prefixPos != std::string::npos) {
                const std::size_t contentLength = std::stoul(all.substr(prefixPos + kPrefix.size()));
                if (all.size() >= headerEnd + 4 + contentLength) {
                    break;
                }
            }
        }
    }
    return all;
}

} // namespace

TEST_CASE("AcpPanel's title row shows the agent name and state", "[AcpPanel]") {
    Fixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession("claude-code");
    fixture.Paint();

    REQUIRE(fixture.RowText(0).find("claude-code") != std::string::npos);
    REQUIRE(fixture.RowText(0).find("[active]") != std::string::npos);
}

TEST_CASE("AcpPanel renders a Plan transcript entry's checkbox glyphs", "[AcpPanel]") {
    Fixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession("claude-code");

    const Json plan = {
        {"jsonrpc", "2.0"},
        {"method", "session/update"},
        {"params",
         {{"sessionId", "s1"},
          {"update",
           {{"sessionUpdate", "plan"}, {"entries", Json::array({Json{{"content", "Trim common suffix"}, {"status", "completed"}}})}}}}},
    };
    fixture.client->DispatchFrame(plan.dump());
    fixture.Paint();

    bool foundCheckedStep = false;
    for (int y = 1; y < kHeight - 1; ++y) {
        if (fixture.RowText(y).find("[x] Trim common suffix") != std::string::npos) {
            foundCheckedStep = true;
        }
    }
    REQUIRE(foundCheckedStep);
}

// diff-preview-line-diff-utility follow-up (ROADMAP "AI-assisted editing
// (ACP) gaps" -- "a real diff view").
TEST_CASE("AcpPanel renders a real +/- diff for a tool call once it is expanded", "[AcpPanel]") {
    Fixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession("claude-code");

    const Json toolCall = {
        {"jsonrpc", "2.0"},
        {"method", "session/update"},
        {"params",
         {{"sessionId", "s1"},
          {"update",
           {{"sessionUpdate", "tool_call"},
            {"toolCallId", "t1"},
            {"title", "Edit foo.txt"},
            {"status", "completed"},
            {"content", Json::array({Json{{"type", "diff"}, {"path", "foo.txt"}, {"oldText", "a\nb\nc\n"}, {"newText", "a\nX\nc\n"}}})}}}}},
    };
    fixture.client->DispatchFrame(toolCall.dump());
    fixture.Paint();

    // Collapsed by default: the header alone, until clicked.
    REQUIRE(fixture.RowText(1).find("▸ • Edit foo.txt") == 0);
    REQUIRE(fixture.ContentText().find("- b") == std::string::npos);
    REQUIRE(fixture.panel.OnEvent(ned::ui::test::Mouse(2, 1, ned::ui::MouseEvent::Button::Left, ned::ui::MouseEvent::Motion::Pressed)));
    fixture.Paint();

    bool sawRemovedBackground = false;
    bool sawAddedBackground   = false;
    for (int y = 1; y < kHeight - 1; ++y) {
        const std::string row = fixture.RowText(y);
        if (row.find("- b") != std::string::npos) {
            REQUIRE(fixture.screen.PixelAt(static_cast<int>(row.find('-')), y).background_color == fixture.theme.diffRemovedBackground);
            sawRemovedBackground = true;
        }
        if (row.find("+ X") != std::string::npos) {
            REQUIRE(fixture.screen.PixelAt(static_cast<int>(row.find('+')), y).background_color == fixture.theme.diffAddedBackground);
            sawAddedBackground = true;
        }
    }
    REQUIRE(sawRemovedBackground);
    REQUIRE(sawAddedBackground);
}

TEST_CASE("AcpPanel renders a pending permission prompt's own diff", "[AcpPanel]") {
    Fixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession("claude-code");

    const Json request = {
        {"jsonrpc", "2.0"},
        {"id", 3},
        {"method", "session/request_permission"},
        {"params",
         {{"sessionId", "s1"},
          {"toolCall",
           {{"title", "Edit foo.txt"}, {"content", Json::array({Json{{"type", "diff"}, {"oldText", "a\n"}, {"newText", "b\n"}}})}}},
          {"options", Json::array({Json{{"optionId", "allow-once"}, {"name", "Allow once"}, {"kind", "allow_once"}}})}}},
    };
    fixture.client->DispatchFrame(request.dump());
    fixture.Paint();

    bool sawRemoved = false;
    bool sawAdded   = false;
    for (int y = 1; y < kHeight - 1; ++y) {
        const std::string row = fixture.RowText(y);
        if (row.find("- a") != std::string::npos) {
            sawRemoved = true;
        }
        if (row.find("+ b") != std::string::npos) {
            sawAdded = true;
        }
    }
    REQUIRE(sawRemoved);
    REQUIRE(sawAdded);
}

TEST_CASE("AcpPanel's input row shows typed text and a caret", "[AcpPanel]") {
    Fixture fixture;

    REQUIRE(fixture.panel.OnEvent(ned::ui::test::Character('h')));
    REQUIRE(fixture.panel.OnEvent(ned::ui::test::Character('i')));
    fixture.Paint();

    const std::string inputRow = fixture.RowText(kHeight - 1);
    REQUIRE(inputRow.find("Prompt: hi") != std::string::npos);
    // block-cursor-readability follow-up: a real recolored block, not a
    // video-invert (which left the character underneath unreadable once the
    // caret moved back over already-typed text) -- see AcpPanel::Paint's own
    // comment.
    const ned::ui::Cell& caretCell = fixture.screen.PixelAt(static_cast<int>(std::string("Prompt: hi").size()), kHeight - 1);
    REQUIRE(caretCell.background_color == fixture.theme.echoArea.foreground);
    REQUIRE(caretCell.foreground_color == fixture.theme.background);
}

TEST_CASE("AcpPanel's input row places the caret by column, not byte, once multi-byte text is typed", "[AcpPanel]") {
    // chrome-widget-utf8 follow-up regression: the old byte-indexed caret
    // (caretCol = text.size(), a byte count) and content-row painting would
    // have split "é" (0xC3 0xA9) across two Cells and put the caret one
    // column too far right. PaintUtf8Row fixes both.
    Fixture fixture;

    fixture.panel.OnEvent(ned::ui::test::Character('h'));
    REQUIRE(fixture.panel.OnEvent(ned::ui::test::Character(U'é')));
    REQUIRE(fixture.panel.OnEvent(ned::ui::test::Character('i')));
    fixture.Paint();

    const std::string prefix = "Prompt: h"; // one column per codepoint up to here
    REQUIRE(fixture.screen.PixelAt(static_cast<int>(prefix.size()), kHeight - 1).character == "é");
    REQUIRE(fixture.screen.PixelAt(static_cast<int>(prefix.size()) + 1, kHeight - 1).character == "i");
    const ned::ui::Cell& caretCell = fixture.screen.PixelAt(static_cast<int>(prefix.size()) + 2, kHeight - 1);
    REQUIRE(caretCell.background_color == fixture.theme.echoArea.foreground);
    REQUIRE(caretCell.foreground_color == fixture.theme.background);
}

TEST_CASE("AcpPanel's Backspace deletes the last typed character", "[AcpPanel]") {
    Fixture fixture;
    fixture.panel.OnEvent(ned::ui::test::Character('h'));
    fixture.panel.OnEvent(ned::ui::test::Character('i'));
    REQUIRE(fixture.panel.OnEvent(ned::ui::test::Backspace()));
    fixture.Paint();

    REQUIRE(fixture.RowText(kHeight - 1).find("Prompt: h") != std::string::npos);
    REQUIRE(fixture.RowText(kHeight - 1).find("Prompt: hi") == std::string::npos);
}

TEST_CASE("AcpPanel's Enter sends the typed prompt through Manager and clears the input row", "[AcpPanel]") {
    Fixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession("claude-code");

    fixture.panel.OnEvent(ned::ui::test::Character('h'));
    fixture.panel.OnEvent(ned::ui::test::Character('i'));
    REQUIRE(fixture.panel.OnEvent(ned::ui::test::Return()));

    const Json promptRequest = fixture.reader.Next();
    REQUIRE(promptRequest["method"] == "session/prompt");
    REQUIRE(promptRequest["params"]["prompt"][0]["text"] == "hi");

    fixture.Paint();
    REQUIRE(fixture.RowText(kHeight - 1) == "Prompt:");
}

// ACP context auto-attach follow-up.
TEST_CASE("AcpPanel's @buffer mention attaches the current buffer's content and replaces the token in the sent text",
          "[AcpPanel]") {
    const ProjectRootGuard      rootGuard;
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_acp_buffer_mention_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    ned::editor::SetProjectRoot(dir);

    Fixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession("claude-code");

    ned::text::Buffer contextBuffer("scratch.cpp");
    contextBuffer.InsertAtPoint("int main() {}\n");
    ned::ui::ActiveBuffer activeBuffer(contextBuffer);
    fixture.panel.SetActiveBufferProvider([&activeBuffer]() -> ned::ui::ActiveBuffer& { return activeBuffer; });

    for (char c : std::string("@buffer")) {
        REQUIRE(fixture.panel.OnEvent(ned::ui::test::Character(c)));
    }
    REQUIRE(fixture.panel.OnEvent(ned::ui::test::Return())); // accepts the mention into the composer
    REQUIRE(fixture.panel.OnEvent(ned::ui::test::Return())); // sends

    const Json promptRequest = fixture.reader.Next();
    REQUIRE(promptRequest["method"] == "session/prompt");
    // No embeddedContext support declared (StartActiveSession's
    // Json::object() initialize result) -- the attachment folds into the
    // single text block.
    REQUIRE(promptRequest["params"]["prompt"].size() == 1);
    const std::string text = promptRequest["params"]["prompt"][0]["text"].get<std::string>();
    REQUIRE(text.find("[attached: scratch.cpp]") != std::string::npos);
    REQUIRE(text.find("int main() {}") != std::string::npos);

    // The transcript shows the compact marker only, not the folded-in
    // content -- PushTranscriptEntry already ran synchronously inside
    // SendPrompt, no response dispatch needed to observe it.
    const std::string outputText = fixture.manager.Transcript().back().text;
    REQUIRE(outputText.find("[attached: scratch.cpp]") != std::string::npos);
    REQUIRE(outputText.find("int main() {}") == std::string::npos);
}

TEST_CASE("AcpPanel's @selection mention attaches only the current selection, not the whole buffer", "[AcpPanel]") {
    const ProjectRootGuard      rootGuard;
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_acp_selection_mention_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    ned::editor::SetProjectRoot(dir);

    Fixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession("claude-code");

    ned::text::Buffer contextBuffer("scratch.cpp");
    contextBuffer.InsertAtPoint("line one\nline two\nline three\n");
    contextBuffer.SetMark(9);   // start of "line two"
    contextBuffer.SetPoint(17); // end of "line two", before its newline
    ned::ui::ActiveBuffer activeBuffer(contextBuffer);
    fixture.panel.SetActiveBufferProvider([&activeBuffer]() -> ned::ui::ActiveBuffer& { return activeBuffer; });

    for (char c : std::string("@selection")) {
        REQUIRE(fixture.panel.OnEvent(ned::ui::test::Character(c)));
    }
    REQUIRE(fixture.panel.OnEvent(ned::ui::test::Return()));
    REQUIRE(fixture.panel.OnEvent(ned::ui::test::Return()));

    const Json        promptRequest = fixture.reader.Next();
    const std::string text          = promptRequest["params"]["prompt"][0]["text"].get<std::string>();
    REQUIRE(text.find("line two") != std::string::npos);
    REQUIRE(text.find("line one") == std::string::npos);
    REQUIRE(text.find("line three") == std::string::npos);
}

TEST_CASE("AcpPanel's Escape and its close (x) button both invoke the toggle callback", "[AcpPanel]") {
    Fixture fixture;
    int     toggles = 0;
    fixture.panel.SetOnToggleRequest([&toggles] { ++toggles; });

    REQUIRE(fixture.panel.OnEvent(ned::ui::test::Escape()));
    REQUIRE(toggles == 1);

    fixture.panel.OnEvent(
        ned::ui::test::Mouse(kWidth - 3, 0, ned::ui::MouseEvent::Button::Left, ned::ui::MouseEvent::Motion::Pressed));
    REQUIRE(toggles == 2);
}

TEST_CASE("AcpPanel shows the pending permission prompt's options, display-only", "[AcpPanel]") {
    Fixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession("claude-code");

    const Json request = {
        {"jsonrpc", "2.0"},
        {"id", 3},
        {"method", "session/request_permission"},
        {"params",
         {{"sessionId", "s1"},
          {"toolCall", {{"title", "Edit main.cpp"}}},
          {"options", Json::array({Json{{"optionId", "allow-once"}, {"name", "Allow once"}, {"kind", "allow_once"}}})}}},
    };
    fixture.client->DispatchFrame(request.dump());
    fixture.Paint();

    bool foundDescription = false;
    bool foundOption      = false;
    for (int y = 1; y < kHeight - 1; ++y) {
        const std::string row = fixture.RowText(y);
        if (row.find("Edit main.cpp") != std::string::npos) {
            foundDescription = true;
        }
        if (row.find("[1] Allow once") != std::string::npos) {
            foundOption = true;
        }
    }
    REQUIRE(foundDescription);
    REQUIRE(foundOption);
}

// ACP round-1-live-validation follow-up: OnEvent itself resolving a pending
// permission (below) is the actual fix for the "deliberate v1 cut" the class
// header used to document -- WindowManager::SetAcpPanelFocusChecker is what
// decides whether a keystroke is routed here instead of BufferView's own
// echo-area flow, a routing concern this panel's own OnEvent doesn't need to
// know about.
TEST_CASE("AcpPanel's digit keys resolve a pending permission prompt", "[AcpPanel]") {
    Fixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession("claude-code");

    const Json request = {
        {"jsonrpc", "2.0"},
        {"id", 3},
        {"method", "session/request_permission"},
        {"params",
         {{"sessionId", "s1"},
          {"toolCall", {{"title", "Edit main.cpp"}}},
          {"options",
           Json::array({Json{{"optionId", "deny"}, {"name", "Deny"}, {"kind", "reject_once"}},
                        Json{{"optionId", "allow-once"}, {"name", "Allow once"}, {"kind", "allow_once"}}})}}},
    };
    fixture.client->DispatchFrame(request.dump());

    REQUIRE(fixture.manager.PendingPermissionPrompt().has_value());
    REQUIRE(fixture.panel.OnEvent(ned::ui::test::Character('2')));
    REQUIRE_FALSE(fixture.manager.PendingPermissionPrompt().has_value());

    const Json response = fixture.reader.Next();
    REQUIRE(response["id"] == 3);
    REQUIRE(response["result"]["outcome"]["outcome"] == "selected");
    REQUIRE(response["result"]["outcome"]["optionId"] == "allow-once");
}

TEST_CASE("AcpPanel word-wraps a long agent message instead of truncating it", "[AcpPanel]") {
    Fixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession("claude-code");

    const Json chunk = {
        {"jsonrpc", "2.0"},
        {"method", "session/update"},
        {"params",
         {{"sessionId", "s1"},
          {"update",
           {{"sessionUpdate", "agent_message_chunk"},
            {"content", {{"type", "text"}, {"text", "one two three four five six seven eight nine ten"}}}}}}},
    };
    fixture.client->DispatchFrame(chunk.dump());
    fixture.Paint();

    std::string joined;
    for (int y = 1; y < kHeight - 1; ++y) {
        joined += fixture.RowText(y) + " ";
    }
    REQUIRE(joined.find("nine ten") != std::string::npos); // the tail of the message actually made it onto screen
    for (int y = 1; y < kHeight - 1; ++y) {
        REQUIRE(static_cast<int>(fixture.RowText(y).size()) <= kWidth); // never spills past the panel's own width
    }
}

TEST_CASE("AcpPanel renders **bold** and `code` Markdown spans in an agent message, stripping the markup", "[AcpPanel]") {
    Fixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession("claude-code");

    const Json chunk = {
        {"jsonrpc", "2.0"},
        {"method", "session/update"},
        {"params",
         {{"sessionId", "s1"},
          {"update",
           {{"sessionUpdate", "agent_message_chunk"}, {"content", {{"type", "text"}, {"text", "plain **bold** and `code` here"}}}}}}},
    };
    fixture.client->DispatchFrame(chunk.dump());
    fixture.Paint();

    const std::string row = fixture.RowText(1);
    REQUIRE(row.find("plain bold and code here") != std::string::npos); // ** and ` markers themselves are stripped

    const std::size_t boldStart = row.find("bold");
    REQUIRE(boldStart != std::string::npos);
    REQUIRE(fixture.screen.PixelAt(static_cast<int>(boldStart), 1).bold);
    // "plain " itself stays un-bolded -- only the marked span is affected.
    REQUIRE_FALSE(fixture.screen.PixelAt(0, 1).bold);

    const std::size_t codeStart = row.find("code");
    REQUIRE(codeStart != std::string::npos);
    REQUIRE(fixture.screen.PixelAt(static_cast<int>(codeStart), 1).background_color == fixture.theme.documentHighlightBackground);
    REQUIRE(fixture.screen.PixelAt(0, 1).background_color != fixture.theme.documentHighlightBackground);
}

TEST_CASE("AcpPanel renders a Markdown bullet marker as a glyph, not literal '- '", "[AcpPanel]") {
    Fixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession("claude-code");

    const Json chunk = {
        {"jsonrpc", "2.0"},
        {"method", "session/update"},
        {"params",
         {{"sessionId", "s1"},
          {"update", {{"sessionUpdate", "agent_message_chunk"}, {"content", {{"type", "text"}, {"text", "- first item"}}}}}}},
    };
    fixture.client->DispatchFrame(chunk.dump());
    fixture.Paint();

    const std::string row = fixture.RowText(1);
    REQUIRE(row.find("- first item") == std::string::npos);
    REQUIRE(row.find("first item") != std::string::npos);
}

TEST_CASE("AcpPanel's composer grows past one row once typed text wraps, and keeps the caret visible", "[AcpPanel]") {
    Fixture fixture;

    // kWidth=40, "Prompt: " is 8 columns -- past ~32 more characters the
    // composer must wrap to a second row instead of running text off-screen.
    const std::string typed = "this prompt is long enough to wrap onto a second composer row";
    for (const char ch : typed) {
        fixture.panel.OnEvent(ned::ui::test::Character(ch));
    }
    fixture.Paint();

    // Every codepoint the user typed appears somewhere in the last two
    // painted rows -- nothing got silently dropped off the right edge.
    const std::string tail = fixture.RowText(kHeight - 2) + fixture.RowText(kHeight - 1);
    REQUIRE(tail.find("second composer row") != std::string::npos);

    bool foundCaret = false;
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            const ned::ui::Cell& cell = fixture.screen.PixelAt(x, y);
            if (cell.background_color == fixture.theme.echoArea.foreground &&
                cell.foreground_color == fixture.theme.background) {
                foundCaret = true;
            }
        }
    }
    REQUIRE(foundCaret); // caret still rendered somewhere once the composer spans multiple rows
}

// Prose-check-the-composer follow-up (ROADMAP "Prose-check the ACP
// composer").
TEST_CASE("AcpPanel underlines a prose diagnostic in the composer once the checker responds", "[AcpPanel]") {
    Fixture   fixture;
    const int originalDebounceMs = ned::editor::lsp::DiagnosticsDebounceMs();
    ned::editor::lsp::SetLspDiagnosticsDebounceMs(50);

    int clientWritesHere[2];
    int clientReadsHere[2];
    REQUIRE(::pipe(clientWritesHere) == 0);
    REQUIRE(::pipe(clientReadsHere) == 0);
    auto proseClientPtr = std::make_unique<ned::editor::lsp::Client>(
        ned::editor::lsp::Transport(clientReadsHere[0], clientWritesHere[1]), fixture.eventLoop);
    ned::editor::lsp::Client& proseClient =
        fixture.lspManager.SetClientForTesting(std::string(kProseLanguageKey), std::move(proseClientPtr));

    // "typo hear" -- byte offsets [5, 9) cover "hear".
    for (const char ch : std::string("typo hear")) {
        fixture.panel.OnEvent(ned::ui::test::Character(ch));
    }
    fixture.Paint(); // Paint() is what calls RequestProseCheckIfNeeded

    WaitUntil(fixture.eventLoop, [&] {
        pollfd pfd{.fd = clientWritesHere[0], .events = POLLIN, .revents = 0};
        return ::poll(&pfd, 1, 0) > 0;
    });
    const std::string openRaw  = ReadRawFrame(clientWritesHere[0]);
    const Json        openJson = Json::parse(openRaw.substr(openRaw.find("\r\n\r\n") + 4));
    REQUIRE(openJson["method"] == "textDocument/didOpen");
    const std::string uri = openJson["params"]["textDocument"]["uri"].get<std::string>();

    const Json publish = {
        {"jsonrpc", "2.0"},
        {"method", "textDocument/publishDiagnostics"},
        {"params",
         {{"uri", uri},
          {"diagnostics", Json::array({{{"range", {{"start", {{"line", 0}, {"character", 5}}}, {"end", {{"line", 0}, {"character", 9}}}}},
                                        {"severity", 4},
                                        {"message", "possible typo: hear"}}})}}},
    };
    proseClient.DispatchFrame(publish.dump());

    // The composer's own diagnostics member is only updated by the callback
    // Manager invokes -- repaint in the wait loop so a caught-up Paint()
    // is what the predicate actually observes.
    WaitUntil(fixture.eventLoop, [&] {
        fixture.Paint();
        // "Prompt: " is 8 columns; "hear" starts at byte 5 -> column 13.
        return fixture.screen.PixelAt(13, kHeight - 1).underlined;
    });

    REQUIRE(fixture.screen.PixelAt(13, kHeight - 1).underlined);       // h
    REQUIRE(fixture.screen.PixelAt(14, kHeight - 1).underlined);       // e
    REQUIRE(fixture.screen.PixelAt(15, kHeight - 1).underlined);       // a
    REQUIRE(fixture.screen.PixelAt(16, kHeight - 1).underlined);       // r
    REQUIRE_FALSE(fixture.screen.PixelAt(12, kHeight - 1).underlined); // the space before "hear"
    REQUIRE_FALSE(fixture.screen.PixelAt(9, kHeight - 1).underlined);  // inside "typo"

    ned::editor::lsp::SetLspDiagnosticsDebounceMs(originalDebounceMs);
    ::close(clientWritesHere[0]);
    ::close(clientReadsHere[1]);
}

TEST_CASE("AcpPanel's Escape cancels a pending permission prompt instead of toggling the panel", "[AcpPanel]") {
    Fixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession("claude-code");
    int toggles = 0;
    fixture.panel.SetOnToggleRequest([&toggles] { ++toggles; });

    const Json request = {
        {"jsonrpc", "2.0"},
        {"id", 3},
        {"method", "session/request_permission"},
        {"params",
         {{"sessionId", "s1"},
          {"toolCall", {{"title", "Edit main.cpp"}}},
          {"options", Json::array({Json{{"optionId", "allow-once"}, {"name", "Allow once"}, {"kind", "allow_once"}}})}}},
    };
    fixture.client->DispatchFrame(request.dump());

    REQUIRE(fixture.panel.OnEvent(ned::ui::test::Escape()));
    REQUIRE(toggles == 0); // cancelled the permission, did not close the panel
    REQUIRE_FALSE(fixture.manager.PendingPermissionPrompt().has_value());

    const Json response = fixture.reader.Next();
    REQUIRE(response["id"] == 3);
    REQUIRE(response["result"]["outcome"]["outcome"] == "cancelled");
}

TEST_CASE("AcpPanel's Control-Left/Right move the composer cursor by word", "[AcpPanel]") {
    Fixture fixture;
    fixture.panel.OnEvent(ned::ui::test::Character('f'));
    fixture.panel.OnEvent(ned::ui::test::Character('o'));
    fixture.panel.OnEvent(ned::ui::test::Character('o'));
    fixture.panel.OnEvent(ned::ui::test::Character(' '));
    fixture.panel.OnEvent(ned::ui::test::Character('b'));
    fixture.panel.OnEvent(ned::ui::test::Character('a'));
    fixture.panel.OnEvent(ned::ui::test::Character('r'));
    // cursor is now at the end of "foo bar"

    REQUIRE(fixture.panel.OnEvent(ned::ui::test::ArrowLeftCtrl()));
    fixture.panel.OnEvent(ned::ui::test::Character('X')); // inserted at the start of "bar"

    fixture.InjectClient();
    fixture.StartActiveSession("claude-code");
    fixture.panel.OnEvent(ned::ui::test::Return());
    const Json promptRequest = fixture.reader.Next();
    REQUIRE(promptRequest["params"]["prompt"][0]["text"] == "foo Xbar");
    fixture.client->DispatchFrame(ResultFrame(promptRequest["id"], Json{{"stopReason", "end_turn"}}));
}

TEST_CASE("AcpPanel's Up/Down recall previously sent prompts, shell-history style", "[AcpPanel]") {
    Fixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession("claude-code");

    fixture.panel.OnEvent(ned::ui::test::Character('f'));
    fixture.panel.OnEvent(ned::ui::test::Character('i'));
    fixture.panel.OnEvent(ned::ui::test::Character('r'));
    fixture.panel.OnEvent(ned::ui::test::Character('s'));
    fixture.panel.OnEvent(ned::ui::test::Character('t'));
    fixture.panel.OnEvent(ned::ui::test::Return());
    Json firstRequest = fixture.reader.Next(); // the session/prompt request for "first"
    fixture.client->DispatchFrame(ResultFrame(firstRequest["id"], Json{{"stopReason", "end_turn"}}));

    fixture.panel.OnEvent(ned::ui::test::Character('s'));
    fixture.panel.OnEvent(ned::ui::test::Character('e'));
    fixture.panel.OnEvent(ned::ui::test::Character('c'));
    fixture.panel.OnEvent(ned::ui::test::Return());
    Json secondRequest = fixture.reader.Next(); // the session/prompt request for "sec"
    fixture.client->DispatchFrame(ResultFrame(secondRequest["id"], Json{{"stopReason", "end_turn"}}));

    fixture.panel.OnEvent(ned::ui::test::Character('d')); // an in-progress, unsent draft
    REQUIRE(fixture.panel.OnEvent(ned::ui::test::ArrowUp()));
    fixture.Paint();
    REQUIRE(fixture.RowText(kHeight - 1) == "Prompt: sec"); // most recent sent prompt

    REQUIRE(fixture.panel.OnEvent(ned::ui::test::ArrowUp()));
    fixture.Paint();
    REQUIRE(fixture.RowText(kHeight - 1) == "Prompt: first");

    REQUIRE(fixture.panel.OnEvent(ned::ui::test::ArrowDown()));
    fixture.Paint();
    REQUIRE(fixture.RowText(kHeight - 1) == "Prompt: sec");

    REQUIRE(fixture.panel.OnEvent(ned::ui::test::ArrowDown()));
    fixture.Paint();
    REQUIRE(fixture.RowText(kHeight - 1) == "Prompt: d"); // back to the unsent draft
}

TEST_CASE("AcpPanel's minimize button and M-m both collapse the panel to a thin strip, click-to-reopen", "[AcpPanel]") {
    Fixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession("claude-code");
    REQUIRE_FALSE(fixture.panel.Collapsed());

    fixture.panel.OnEvent(
        ned::ui::test::Mouse(kWidth - 6, 0, ned::ui::MouseEvent::Button::Left, ned::ui::MouseEvent::Motion::Pressed));
    REQUIRE(fixture.panel.Collapsed());

    fixture.Paint();
    REQUIRE(fixture.RowText(0).find("claude-code") != std::string::npos);
    REQUIRE(fixture.RowText(0).find("minimized") != std::string::npos);

    // The whole collapsed strip is a click-to-reopen target.
    fixture.panel.OnEvent(
        ned::ui::test::Mouse(5, 0, ned::ui::MouseEvent::Button::Left, ned::ui::MouseEvent::Motion::Pressed));
    REQUIRE_FALSE(fixture.panel.Collapsed());

    REQUIRE(fixture.panel.OnEvent(ned::ui::test::Alt('m')));
    REQUIRE(fixture.panel.Collapsed());
    REQUIRE(fixture.panel.OnEvent(ned::ui::test::Alt('m'))); // toggles back
    REQUIRE_FALSE(fixture.panel.Collapsed());
}

TEST_CASE("Double-clicking the resize divider also collapses the standalone AcpPanel (ProjectSidebar/VcsPanel's own convention)",
          "[AcpPanel]") {
    Fixture fixture;
    REQUIRE_FALSE(fixture.panel.Collapsed());

    // Bottom dock (the default): the divider is the title row, at any column
    // the close/minimize buttons don't already claim.
    fixture.panel.OnEvent(ned::ui::test::Mouse(5, 0, ned::ui::MouseEvent::Button::Left, ned::ui::MouseEvent::Motion::Pressed));
    REQUIRE_FALSE(fixture.panel.Collapsed()); // first press starts a resize, not a collapse

    fixture.panel.OnEvent(ned::ui::test::Mouse(5, 0, ned::ui::MouseEvent::Button::Left, ned::ui::MouseEvent::Motion::Pressed));
    REQUIRE(fixture.panel.Collapsed()); // ...the rapid second press collapses instead
}

TEST_CASE("A real drag on the AcpPanel resize divider never counts as the first half of a collapse double-click",
          "[AcpPanel]") {
    Fixture fixture;
    fixture.panel.SetTerminalSize(ned::ui::Size{.width = 100, .height = 100});
    const int original = ned::editor::acp::PanelSizePercent();

    fixture.panel.OnEvent(ned::ui::test::Mouse(5, 0, ned::ui::MouseEvent::Button::Left, ned::ui::MouseEvent::Motion::Pressed));
    fixture.panel.OnEvent(
        ned::ui::test::Mouse(5, 10, ned::ui::MouseEvent::Button::Left, ned::ui::MouseEvent::Motion::Moved)); // a genuine drag
    REQUIRE(ned::editor::acp::PanelSizePercent() != original);
    fixture.panel.OnEvent(
        ned::ui::test::Mouse(5, 10, ned::ui::MouseEvent::Button::Left, ned::ui::MouseEvent::Motion::Released));

    // A prompt new press on the divider (well within the drag's own initial
    // press's double-click window) must start a fresh resize, not collapse.
    fixture.panel.OnEvent(ned::ui::test::Mouse(5, 0, ned::ui::MouseEvent::Button::Left, ned::ui::MouseEvent::Motion::Pressed));
    REQUIRE_FALSE(fixture.panel.Collapsed());
    fixture.panel.OnEvent(
        ned::ui::test::Mouse(5, 0, ned::ui::MouseEvent::Button::Left, ned::ui::MouseEvent::Motion::Released));

    ned::editor::acp::SetAcpPanelSizePercent(original); // cleanup -- process-wide state
}

TEST_CASE("AcpPanel::SetOnCollapseChanged fires only on an actual state change", "[AcpPanel]") {
    // panel-resize/minimize regression: OverlayHost only recomputes this
    // panel's on-screen Box from Show()/Reflow(), never on every Paint() --
    // so main.cpp relies on this callback firing exactly when Collapsed()
    // actually flips, to force a Box recompute (overlays.Show) immediately
    // instead of leaving a stale, wrongly-sized Box in place. A no-op
    // SetCollapsed call (already at that value) must not force a needless
    // recompute.
    Fixture fixture;
    int     changes = 0;
    fixture.panel.SetOnCollapseChanged([&changes] { ++changes; });

    fixture.panel.SetCollapsed(false); // already false -- no-op
    REQUIRE(changes == 0);

    fixture.panel.SetCollapsed(true);
    REQUIRE(changes == 1);
    REQUIRE(fixture.panel.Collapsed());

    fixture.panel.SetCollapsed(true); // already true -- no-op
    REQUIRE(changes == 1);

    fixture.panel.ToggleCollapsed();
    REQUIRE(changes == 2);
    REQUIRE_FALSE(fixture.panel.Collapsed());
}

TEST_CASE("AcpPanel's Control-Up/Down grow/shrink PanelSizePercent", "[AcpPanel]") {
    const int original = ned::editor::acp::PanelSizePercent();
    ned::editor::acp::SetAcpPanelSizePercent(30);

    Fixture fixture;
    REQUIRE(fixture.panel.OnEvent(ned::ui::test::ArrowUpCtrl()));
    REQUIRE(ned::editor::acp::PanelSizePercent() == 35);

    REQUIRE(fixture.panel.OnEvent(ned::ui::test::ArrowDownCtrl()));
    REQUIRE(fixture.panel.OnEvent(ned::ui::test::ArrowDownCtrl()));
    REQUIRE(ned::editor::acp::PanelSizePercent() == 25);

    ned::editor::acp::SetAcpPanelSizePercent(original); // cleanup -- process-wide state
}

TEST_CASE("AcpPanel's composer opens an @-mention picker listing fuzzy-matched project files", "[AcpPanel]") {
    const ProjectRootGuard rootGuard;

    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_acp_mention_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    std::ofstream(dir / "alpha.txt") << "a";
    std::ofstream(dir / "beta.txt") << "b";
    ned::editor::SetProjectRoot(dir);

    Fixture fixture;
    REQUIRE(fixture.panel.OnEvent(ned::ui::test::Character('@')));
    fixture.Paint();

    bool foundAlpha = false, foundBeta = false;
    for (int y = 1; y < kHeight - 1; ++y) {
        const std::string row = fixture.RowText(y);
        if (row.find("alpha.txt") != std::string::npos) {
            foundAlpha = true;
        }
        if (row.find("beta.txt") != std::string::npos) {
            foundBeta = true;
        }
    }
    REQUIRE(foundAlpha);
    REQUIRE(foundBeta);

    // Narrowing the query to "al" should fuzzy-filter beta.txt out.
    REQUIRE(fixture.panel.OnEvent(ned::ui::test::Character('a')));
    REQUIRE(fixture.panel.OnEvent(ned::ui::test::Character('l')));
    fixture.Paint();

    bool stillFoundBeta = false;
    for (int y = 1; y < kHeight - 1; ++y) {
        if (fixture.RowText(y).find("beta.txt") != std::string::npos) {
            stillFoundBeta = true;
        }
    }
    REQUIRE_FALSE(stillFoundBeta);
}

TEST_CASE("AcpPanel's @-mention picker inserts the selected path and closes on Enter", "[AcpPanel]") {
    const ProjectRootGuard rootGuard;

    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_acp_mention_accept_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    std::ofstream(dir / "readme.md") << "hello";
    ned::editor::SetProjectRoot(dir);

    Fixture fixture;
    REQUIRE(fixture.panel.OnEvent(ned::ui::test::Character('@')));
    REQUIRE(fixture.panel.OnEvent(ned::ui::test::Character('r')));
    REQUIRE(fixture.panel.OnEvent(ned::ui::test::Character('e')));
    REQUIRE(fixture.panel.OnEvent(ned::ui::test::Return()));

    REQUIRE(fixture.panel.OnEvent(ned::ui::test::Character('!'))); // continues typing after the inserted mention
    fixture.Paint();

    const std::string inputRow = fixture.RowText(kHeight - 1);
    REQUIRE(inputRow.find("@readme.md !") != std::string::npos);

    // The picker itself must be closed -- the content area shows the (empty)
    // transcript, not "Mention a file".
    for (int y = 1; y < kHeight - 1; ++y) {
        REQUIRE(fixture.RowText(y).find("Mention a file") == std::string::npos);
    }
}

TEST_CASE("AcpPanel's @-mention picker closes on Escape without touching the typed text", "[AcpPanel]") {
    const ProjectRootGuard rootGuard;

    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_acp_mention_escape_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    std::ofstream(dir / "file.txt") << "x";
    ned::editor::SetProjectRoot(dir);

    Fixture fixture;
    REQUIRE(fixture.panel.OnEvent(ned::ui::test::Character('@')));
    REQUIRE(fixture.panel.OnEvent(ned::ui::test::Character('f')));
    REQUIRE(fixture.panel.OnEvent(ned::ui::test::Escape()));
    fixture.Paint();

    REQUIRE(fixture.RowText(kHeight - 1).find("Prompt: @f") != std::string::npos); // typed text untouched
    for (int y = 1; y < kHeight - 1; ++y) {
        REQUIRE(fixture.RowText(y).find("Mention a file") == std::string::npos); // picker itself gone
    }
}

TEST_CASE("AcpPanel does not open an @-mention picker mid-word (e.g. an email-shaped 'user@host')", "[AcpPanel]") {
    const ProjectRootGuard rootGuard;
    ned::editor::SetProjectRoot(std::filesystem::temp_directory_path());

    Fixture fixture;
    for (const char ch : std::string("user@host")) {
        REQUIRE(fixture.panel.OnEvent(ned::ui::test::Character(ch)));
    }
    fixture.Paint();

    for (int y = 1; y < kHeight - 1; ++y) {
        REQUIRE(fixture.RowText(y).find("Mention a file") == std::string::npos);
    }
}

// tabbed-bottom-dock-overlays follow-up: dockHosted_ coverage. Every test
// above exercises the default (dockHosted_ == false, standalone/right-dock)
// path unmodified; these confirm the embedded path PanelDock.h drives
// instead -- see AcpPanel::SetDockHosted's own doc comment for exactly what
// changes. PanelDock's own chrome/hit-testing is covered generically in
// PanelDockTest.cpp against fake panels; these are scoped to AcpPanel's own
// half of the split.

TEST_CASE("AcpPanel dock-hosted mode paints content starting at row 0, no title chrome", "[AcpPanel]") {
    Fixture fixture;
    fixture.panel.SetDockHosted(true);
    fixture.InjectClient();
    fixture.StartActiveSession("claude-code");
    fixture.Paint();

    // TitleText() still reports the same text the standalone title row used
    // to draw -- PanelDock's tab strip is what renders it now.
    REQUIRE(fixture.panel.TitleText().find("claude-code") != std::string::npos);
    REQUIRE(fixture.panel.TitleText().find("[active]") != std::string::npos);
    // No divider glyph drawn anywhere -- row 0 is a real content row now.
    for (int x = 0; x < kWidth; ++x) {
        REQUIRE(fixture.screen.PixelAt(x, 0).character != "─");
    }
}

TEST_CASE("AcpPanel dock-hosted mode ignores the standalone close/minimize/resize-divider hit-tests", "[AcpPanel]") {
    Fixture fixture;
    fixture.panel.SetDockHosted(true);
    int toggles = 0;
    fixture.panel.SetOnToggleRequest([&toggles] { ++toggles; });
    fixture.Paint();

    // Same screen columns the standalone-mode close/minimize buttons used to
    // occupy (see the non-dock-hosted "close (x) button" test above) -- now
    // just plain content-row clicks that take focus and do nothing else.
    REQUIRE(fixture.panel.OnEvent(
        ned::ui::test::Mouse(kWidth - 3, 0, ned::ui::MouseEvent::Button::Left, ned::ui::MouseEvent::Motion::Pressed)));
    REQUIRE(toggles == 0);
    REQUIRE(fixture.panel.OnEvent(
        ned::ui::test::Mouse(kWidth - 6, 0, ned::ui::MouseEvent::Button::Left, ned::ui::MouseEvent::Motion::Pressed)));
    REQUIRE_FALSE(fixture.panel.Collapsed());
    REQUIRE(fixture.panel.Focused());

    // The row-0/left-edge resize-divider carve-out is gone too -- a press at
    // (0, 0) is just an ordinary content click, not BeginResize: resizing_
    // never becomes true, so the drag-follow Moved event below is simply
    // unhandled rather than adjusting PanelSizePercent().
    const int before = ned::editor::acp::PanelSizePercent();
    REQUIRE(fixture.panel.OnEvent(ned::ui::test::Mouse(0, 0, ned::ui::MouseEvent::Button::Left, ned::ui::MouseEvent::Motion::Pressed)));
    REQUIRE_FALSE(fixture.panel.OnEvent(ned::ui::test::Mouse(0, 3, ned::ui::MouseEvent::Button::Left, ned::ui::MouseEvent::Motion::Moved)));
    REQUIRE(ned::editor::acp::PanelSizePercent() == before);
}

TEST_CASE("AcpPanel dock-hosted mode ignores M-m and never collapses", "[AcpPanel]") {
    Fixture fixture;
    fixture.panel.SetDockHosted(true);

    // Falls through unhandled now -- IsPlainCharacter also rejects a Meta
    // chord, so there's nothing left for it to do.
    REQUIRE_FALSE(fixture.panel.OnEvent(ned::ui::test::Alt('m')));
    REQUIRE_FALSE(fixture.panel.Collapsed());
}

namespace {

std::string NumberedLines(int from, int to) {
    std::string text;
    for (int i = from; i <= to; ++i) {
        text += (i == from ? "" : "\n") + std::string("L") + std::to_string(i);
    }
    return text;
}

} // namespace

TEST_CASE("AcpPanel's PageUp scrolls back and new output doesn't move the scrolled-back view", "[AcpPanel][AcpScroll]") {
    Fixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession("claude-code");
    fixture.AgentSays(NumberedLines(1, 12));
    fixture.Paint();
    REQUIRE(fixture.RowText(1) == "L9");

    REQUIRE(fixture.panel.OnEvent(ned::ui::test::PageUp()));
    fixture.Paint();
    REQUIRE(fixture.RowText(1) == "L6");
    REQUIRE(fixture.RowText(4).find("3 more") != std::string::npos);
    REQUIRE(fixture.panel.TitleText().find("(scrollback)") != std::string::npos);

    fixture.AgentSays("\n" + NumberedLines(13, 14));
    fixture.Paint();
    REQUIRE(fixture.RowText(1) == "L6");
    REQUIRE(fixture.RowText(4).find("5 more") != std::string::npos);

    REQUIRE(fixture.panel.OnEvent(ned::ui::test::EndCtrl()));
    fixture.Paint();
    REQUIRE(fixture.RowText(1) == "L11");
    REQUIRE(fixture.RowText(4) == "L14");
    REQUIRE(fixture.panel.TitleText().find("(scrollback)") == std::string::npos);
}

TEST_CASE("AcpPanel's C-Home jumps to the top and the mouse wheel scrolls", "[AcpPanel][AcpScroll]") {
    Fixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession("claude-code");
    fixture.AgentSays(NumberedLines(1, 12));
    fixture.Paint();

    REQUIRE(fixture.panel.OnEvent(ned::ui::test::HomeCtrl()));
    fixture.Paint();
    REQUIRE(fixture.RowText(1) == "L1");

    REQUIRE(fixture.panel.OnEvent(
        ned::ui::test::Mouse(3, 2, ned::ui::MouseEvent::Button::WheelDown, ned::ui::MouseEvent::Motion::Pressed)));
    fixture.Paint();
    REQUIRE(fixture.RowText(1) == "L4");
}

TEST_CASE("AcpPanel's M-Up/M-Down jump between the user's prompts", "[AcpPanel][AcpScroll]") {
    Fixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession("claude-code");
    fixture.manager.SendPrompt("first");
    fixture.AgentSays(NumberedLines(1, 8));
    fixture.manager.SendPrompt("second");
    fixture.AgentSays(NumberedLines(9, 16));
    fixture.Paint();

    REQUIRE(fixture.panel.OnEvent(ned::ui::test::ArrowUpAlt()));
    fixture.Paint();
    REQUIRE(fixture.RowText(1) == "> second");

    REQUIRE(fixture.panel.OnEvent(ned::ui::test::ArrowUpAlt()));
    fixture.Paint();
    REQUIRE(fixture.RowText(1) == "> first");

    REQUIRE(fixture.panel.OnEvent(ned::ui::test::ArrowDownAlt()));
    fixture.Paint();
    REQUIRE(fixture.RowText(1) == "> second");
}

TEST_CASE("AcpPanel's Enter snaps a scrolled-back transcript to the tail", "[AcpPanel][AcpScroll]") {
    Fixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession("claude-code");
    fixture.AgentSays(NumberedLines(1, 12));
    fixture.Paint();
    REQUIRE(fixture.panel.OnEvent(ned::ui::test::HomeCtrl()));
    fixture.Paint();

    fixture.panel.OnEvent(ned::ui::test::Character('x'));
    REQUIRE(fixture.panel.OnEvent(ned::ui::test::Return()));
    fixture.Paint();
    REQUIRE(fixture.RowText(3) == "> x"); // row 4 is the in-flight status row
}

namespace {

Json ToolCallUpdate(const std::string& id, const std::string& status) {
    return {{"sessionUpdate", "tool_call"},
            {"toolCallId", id},
            {"title", "Run tests"},
            {"kind", "execute"},
            {"status", status},
            {"rawInput", {{"command", "ctest -j8"}}},
            {"locations", Json::array({Json{{"path", ned::editor::ProjectRoot().string() + "/Tests/Foo.cpp"}, {"line", 9}}})},
            {"content", Json::array({Json{{"type", "content"}, {"content", {{"type", "text"}, {"text", "100% tests passed"}}}}})}};
}

} // namespace

TEST_CASE("AcpPanel collapses a tool call to one line with its kind glyph, location and status", "[AcpPanel]") {
    Fixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession("claude-code");
    fixture.SendUpdate(ToolCallUpdate("t1", "completed"));
    fixture.Paint();

    const std::string header = fixture.RowText(1);
    REQUIRE(header.find("▸ $ Run tests · Tests/Foo.cpp:10") == 0);
    REQUIRE(header.find("✓") != std::string::npos);
    REQUIRE(fixture.ContentText().find("ctest") == std::string::npos);
}

TEST_CASE("AcpPanel's C-o expands every tool call to show its command, locations and output", "[AcpPanel]") {
    Fixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession("claude-code");
    fixture.SendUpdate(ToolCallUpdate("t1", "completed"));
    REQUIRE(fixture.panel.OnEvent(ned::ui::test::Ctrl('o')));
    fixture.Paint();

    REQUIRE(fixture.RowText(1).find("▾ $ Run tests") == 0);
    REQUIRE(fixture.RowText(2) == "    $ ctest -j8");
    REQUIRE(fixture.RowText(3) == "    ↳ Tests/Foo.cpp:10");
    REQUIRE(fixture.RowText(4) == "    100% tests passed");

    REQUIRE(fixture.panel.OnEvent(ned::ui::test::Ctrl('o')));
    fixture.Paint();
    REQUIRE(fixture.RowText(1).find("▸ $ Run tests") == 0);
}

TEST_CASE("AcpPanel opens a clicked tool location at its 1-based line", "[AcpPanel]") {
    Fixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession("claude-code");
    std::filesystem::path opened;
    std::size_t           openedLine = 0;
    fixture.panel.SetOnOpenLocation([&](const std::filesystem::path& path, std::size_t line) {
        opened     = path;
        openedLine = line;
    });
    fixture.SendUpdate(ToolCallUpdate("t1", "completed"));
    fixture.panel.OnEvent(ned::ui::test::Ctrl('o'));
    fixture.Paint();

    REQUIRE(fixture.panel.OnEvent(ned::ui::test::Mouse(6, 3, ned::ui::MouseEvent::Button::Left, ned::ui::MouseEvent::Motion::Pressed)));
    REQUIRE(opened == ned::editor::ProjectRoot() / "Tests/Foo.cpp");
    REQUIRE(openedLine == 10);
}

TEST_CASE("AcpPanel keeps a tool call's details across updates that omit them", "[AcpPanel]") {
    Fixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession("claude-code");
    fixture.SendUpdate(ToolCallUpdate("t1", "in_progress"));
    fixture.SendUpdate({{"sessionUpdate", "tool_call_update"}, {"toolCallId", "t1"}, {"status", "failed"}});

    const auto& entry = fixture.manager.Transcript().back();
    REQUIRE(entry.status == "failed");
    REQUIRE(entry.toolKind == "execute");
    REQUIRE(entry.toolInput == "$ ctest -j8");
    REQUIRE(entry.toolOutput == "100% tests passed");
    REQUIRE(entry.locations.size() == 1);
    REQUIRE(entry.locations[0].line == 9);

    fixture.Paint();
    REQUIRE(fixture.RowText(1).find("✗") != std::string::npos);
    REQUIRE(fixture.RowText(2) == "failed");
}

TEST_CASE("AcpPanel collapses thinking to a summary line, expands it on click, and hides it on request", "[AcpPanel]") {
    Fixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession("claude-code");
    fixture.SendUpdate({{"sessionUpdate", "agent_thought_chunk"}, {"content", {{"type", "text"}, {"text", "hmm\nlet me see"}}}});
    fixture.AgentSays("Answer");
    fixture.Paint();

    REQUIRE(fixture.RowText(1) == "▸ Thinking (2 lines)");
    REQUIRE(fixture.RowText(2) == "Answer");

    fixture.panel.OnEvent(ned::ui::test::Mouse(1, 1, ned::ui::MouseEvent::Button::Left, ned::ui::MouseEvent::Motion::Pressed));
    fixture.Paint();
    REQUIRE(fixture.RowText(1) == "▾ Thinking");
    REQUIRE(fixture.RowText(2) == "  hmm");
    REQUIRE(fixture.RowText(3) == "  let me see");

    ned::editor::acp::SetAcpThinkingDisplay("hidden");
    fixture.Paint();
    ned::editor::acp::SetAcpThinkingDisplay("collapsed");
    REQUIRE(fixture.RowText(1) == "Answer");
}

namespace {

Json SessionSettings() {
    return {{"configOptions",
             Json::array({Json{{"id", "mode"},
                               {"name", "Mode"},
                               {"category", "mode"},
                               {"currentValue", "default"},
                               {"options", Json::array({Json{{"value", "default"}, {"name", "Manual"}}, Json{{"value", "plan"}, {"name", "Plan"}}})}},
                          Json{{"id", "model"},
                               {"name", "Model"},
                               {"category", "model"},
                               {"currentValue", "opus"},
                               {"options", Json::array({Json{{"value", "opus"}, {"name", "Opus"}}, Json{{"value", "haiku"}, {"name", "Haiku"}}})}},
                          Json{{"id", "fast"}, {"name", "Fast mode"}, {"type", "boolean"}, {"currentValue", false}}})}};
}

} // namespace

TEST_CASE("AcpPanel's status row shows the session's mode and model, and its usage", "[AcpPanel]") {
    Fixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession("claude-code", SessionSettings());
    fixture.SendUpdate({{"sessionUpdate", "usage_update"}, {"used", 1000}, {"size", 4000}});
    fixture.Paint();

    REQUIRE(fixture.RowText(4).find("⏵ Manual · Opus") == 0);
    REQUIRE(fixture.RowText(4).find("ctx 25%") != std::string::npos);

    fixture.SendUpdate({{"sessionUpdate", "session_info_update"}, {"title", "Fix the parser"}});
    REQUIRE(fixture.panel.TitleText() == "claude-code · Fix the parser [active]");
}

TEST_CASE("AcpPanel's S-Tab cycles the session's mode", "[AcpPanel]") {
    Fixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession("claude-code", SessionSettings());

    REQUIRE(fixture.panel.OnEvent(ned::ui::test::TabReverse()));
    const Json request = fixture.reader.Next();
    REQUIRE(request["method"] == "session/set_config_option");
    REQUIRE(request["params"]["configId"] == "mode");
    REQUIRE(request["params"]["value"] == "plan");
}

TEST_CASE("AcpPanel's M-p opens the model picker and a digit picks a model", "[AcpPanel]") {
    Fixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession("claude-code", SessionSettings());

    REQUIRE(fixture.panel.OnEvent(ned::ui::test::Alt('p')));
    fixture.Paint();
    REQUIRE(fixture.RowText(1) == "Model");
    REQUIRE(fixture.RowText(2).find("● Opus") != std::string::npos);

    REQUIRE(fixture.panel.OnEvent(ned::ui::test::Character('2')));
    const Json request = fixture.reader.Next();
    REQUIRE(request["params"]["configId"] == "model");
    REQUIRE(request["params"]["value"] == "haiku");
    REQUIRE(fixture.panel.OnEvent(ned::ui::test::Character('z'))); // the picker closed; this types
    REQUIRE(fixture.panel.TitleText().find("claude-code") == 0);
}

TEST_CASE("AcpPanel's options picker leads to the chosen option's own values", "[AcpPanel]") {
    Fixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession("claude-code", SessionSettings());

    fixture.panel.OpenPicker(ned::editor::acp::PanelPicker::Options);
    fixture.Paint();
    REQUIRE(fixture.RowText(1) == "Setting");
    REQUIRE(fixture.panel.OnEvent(ned::ui::test::Character('3')));
    fixture.Paint();
    REQUIRE(fixture.RowText(1) == "Fast mode");
    REQUIRE(fixture.panel.OnEvent(ned::ui::test::Character('1')));
    const Json request = fixture.reader.Next();
    REQUIRE(request["params"]["configId"] == "fast");
    REQUIRE(request["params"]["value"] == true);
}

TEST_CASE("AcpPanel's model picker says so when the agent offers no model choice", "[AcpPanel]") {
    Fixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession("claude-code");

    fixture.panel.OpenPicker(ned::editor::acp::PanelPicker::Model);
    fixture.Paint();
    REQUIRE(fixture.RowText(1) == "This agent offers no model choice");
    REQUIRE(fixture.panel.OnEvent(ned::ui::test::Escape()));
    fixture.Paint();
    REQUIRE(fixture.RowText(1).empty());
}

namespace {

void AdvertiseCommands(Fixture& fixture) {
    fixture.SendUpdate({{"sessionUpdate", "available_commands_update"},
                        {"availableCommands",
                         Json::array({Json{{"name", "compact"}, {"description", "Summarize the conversation"}, {"input", nullptr}},
                                      Json{{"name", "review"}, {"description", "Review a PR"}, {"input", {{"hint", "PR number"}}}}})}});
}

void Type(Fixture& fixture, std::string_view text) {
    for (const char ch : text) {
        fixture.panel.OnEvent(ned::ui::test::Character(ch));
    }
}

} // namespace

TEST_CASE("AcpPanel completes the agent's slash commands, showing each one's input hint", "[AcpPanel]") {
    Fixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession("claude-code");
    AdvertiseCommands(fixture);

    Type(fixture, "/rev");
    fixture.Paint();
    REQUIRE(fixture.ContentText().find("> /review <PR number>") != std::string::npos);

    REQUIRE(fixture.panel.OnEvent(ned::ui::test::Tab()));
    fixture.Paint();
    REQUIRE(fixture.RowText(kHeight - 1) == "Prompt: /review");
    REQUIRE(fixture.ContentText().find("Command (") == std::string::npos);
}

TEST_CASE("AcpPanel's Enter on a slash command that takes no input runs it", "[AcpPanel]") {
    Fixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession("claude-code");
    AdvertiseCommands(fixture);

    Type(fixture, "/co");
    REQUIRE(fixture.panel.OnEvent(ned::ui::test::Return()));
    const Json request = fixture.reader.Next();
    REQUIRE(request["method"] == "session/prompt");
    REQUIRE(request["params"]["prompt"][0]["text"] == "/compact");
}

TEST_CASE("AcpPanel only completes a slash command as the prompt's first word", "[AcpPanel]") {
    Fixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession("claude-code");
    AdvertiseCommands(fixture);

    Type(fixture, "see /co");
    fixture.Paint();
    REQUIRE(fixture.ContentText().find("Command (") == std::string::npos);
}

TEST_CASE("AcpPanel sends an @file mention as a resource_link and keeps the mention in the text", "[AcpPanel]") {
    const ProjectRootGuard      rootGuard;
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_acp_link_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "src");
    std::ofstream(dir / "src" / "a.txt") << "a";
    ned::editor::SetProjectRoot(dir);

    Fixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession("claude-code");
    Type(fixture, "look at @src/a.txt and @nobody");
    fixture.panel.OnEvent(ned::ui::test::Escape()); // dismiss the completion list
    REQUIRE(fixture.panel.OnEvent(ned::ui::test::Return()));

    const Json  request = fixture.reader.Next();
    const Json& prompt  = request["params"]["prompt"];
    REQUIRE(prompt.size() == 2);
    REQUIRE(prompt[0]["text"] == "look at @src/a.txt and @nobody");
    REQUIRE(prompt[1]["type"] == "resource_link");
    REQUIRE(prompt[1]["uri"] == "file://" + (dir / "src" / "a.txt").string());
    REQUIRE(prompt[1]["name"] == "src/a.txt");
    REQUIRE(fixture.manager.Transcript().back().text == "look at @src/a.txt and @nobody");
    std::filesystem::remove_all(dir);
}

TEST_CASE("AcpPanel's session picker lists the agent's sessions and resumes the one picked", "[AcpPanel]") {
    Fixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession("claude-code", Json::object(),
                               {{"agentCapabilities", {{"loadSession", true}, {"sessionCapabilities", {{"list", Json::object()}}}}}});

    fixture.panel.OpenPicker(ned::editor::acp::PanelPicker::Sessions);
    fixture.Paint();
    REQUIRE(fixture.RowText(1) == "Loading sessions…");

    const Json list = fixture.reader.Next();
    REQUIRE(list["method"] == "session/list");
    fixture.client->DispatchFrame(ResultFrame(
        list["id"], Json{{"sessions", Json::array({Json{{"sessionId", "s1"}, {"title", "This one"}},
                                                   Json{{"sessionId", "old"}, {"title", "Parser work"}, {"updatedAt", "2020-01-02T03:04:05Z"}}})}}));
    fixture.Paint();
    REQUIRE(fixture.RowText(1) == "Resume which session?");
    REQUIRE(fixture.RowText(2).find("● This one") != std::string::npos);
    REQUIRE(fixture.RowText(3).find("Parser work  2020-01-02") != std::string::npos);

    REQUIRE(fixture.panel.OnEvent(ned::ui::test::Character('2')));
    const Json load = fixture.reader.Next();
    REQUIRE(load["method"] == "session/load");
    REQUIRE(load["params"]["sessionId"] == "old");
}

TEST_CASE("AcpPanel's session picker deletes a session after a y, but not the current one", "[AcpPanel]") {
    Fixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession(
        "claude-code", Json::object(),
        {{"agentCapabilities", {{"loadSession", true}, {"sessionCapabilities", {{"list", Json::object()}, {"delete", Json::object()}}}}}});

    fixture.panel.OpenPicker(ned::editor::acp::PanelPicker::Sessions);
    const Json list = fixture.reader.Next();
    fixture.client->DispatchFrame(ResultFrame(
        list["id"], Json{{"sessions", Json::array({Json{{"sessionId", "s1"}, {"title", "This one"}}, Json{{"sessionId", "old"}, {"title", "Parser work"}}})}}));
    fixture.Paint();
    REQUIRE(fixture.RowText(1) == "Resume which session? (Del deletes)");

    // The current session stays.
    fixture.panel.OnEvent(ned::ui::test::Delete());
    fixture.panel.OnEvent(ned::ui::test::Character('y'));
    fixture.Paint();
    REQUIRE(fixture.RowText(2).find("This one") != std::string::npos);

    // n keeps it; y deletes.
    fixture.panel.OnEvent(ned::ui::test::ArrowDown());
    fixture.panel.OnEvent(ned::ui::test::Delete());
    fixture.Paint();
    REQUIRE(fixture.RowText(1) == "Delete \"Parser work\"? (y/n)");
    fixture.panel.OnEvent(ned::ui::test::Character('n'));
    fixture.Paint();
    REQUIRE(fixture.RowText(3).find("Parser work") != std::string::npos);
    fixture.panel.OnEvent(ned::ui::test::Delete());
    fixture.panel.OnEvent(ned::ui::test::Character('y'));
    const Json del = fixture.reader.Next();
    REQUIRE(del["method"] == "session/delete");
    REQUIRE(del["params"]["sessionId"] == "old");
    fixture.client->DispatchFrame(ResultFrame(del["id"], Json::object()));
    fixture.Paint();
    REQUIRE(fixture.RowText(3).find("Parser work") == std::string::npos);
    REQUIRE(fixture.ContentText().find("Deleted Parser work") != std::string::npos);
}

TEST_CASE("AcpPanel drops a session listing that arrives after its picker was dismissed", "[AcpPanel]") {
    Fixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession("claude-code", Json::object(),
                               {{"agentCapabilities", {{"loadSession", true}, {"sessionCapabilities", {{"list", Json::object()}}}}}});

    fixture.panel.OpenPicker(ned::editor::acp::PanelPicker::Sessions);
    const Json list = fixture.reader.Next();
    fixture.panel.OnEvent(ned::ui::test::Escape());
    const Json cancel = fixture.reader.Next();
    REQUIRE(cancel["method"] == "$/cancel_request");
    REQUIRE(cancel["params"]["requestId"] == list["id"]);
    REQUIRE_FALSE(cancel.contains("id"));
    fixture.client->DispatchFrame(ResultFrame(list["id"], Json{{"sessions", Json::array({Json{{"sessionId", "old"}}})}}));
    fixture.Paint();
    REQUIRE(fixture.ContentText().find("Resume which session?") == std::string::npos);
}

TEST_CASE("AcpPanel queues a prompt sent mid-turn, shows it, and Up takes it back to edit", "[AcpPanel]") {
    Fixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession("claude-code");
    Type(fixture, "first");
    fixture.panel.OnEvent(ned::ui::test::Return());
    (void)fixture.reader.Next();

    Type(fixture, "second");
    fixture.panel.OnEvent(ned::ui::test::Return());
    REQUIRE(fixture.manager.QueuedPrompts().size() == 1);
    fixture.Paint();
    REQUIRE(fixture.RowText(3) == "⧗ second");

    REQUIRE(fixture.panel.OnEvent(ned::ui::test::ArrowUp()));
    REQUIRE(fixture.manager.QueuedPrompts().empty());
    fixture.Paint();
    REQUIRE(fixture.RowText(kHeight - 1) == "Prompt: second");
}

TEST_CASE("AcpPanel's Escape mid-turn cancels and hands queued prompts back to the composer", "[AcpPanel]") {
    Fixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession("claude-code");
    Type(fixture, "first");
    fixture.panel.OnEvent(ned::ui::test::Return());
    (void)fixture.reader.Next();
    Type(fixture, "second");
    fixture.panel.OnEvent(ned::ui::test::Return());

    REQUIRE(fixture.panel.OnEvent(ned::ui::test::Escape()));
    REQUIRE(fixture.reader.Next()["method"] == "session/cancel");
    REQUIRE(fixture.manager.QueuedPrompts().empty());
    fixture.Paint();
    REQUIRE(fixture.RowText(kHeight - 1) == "Prompt: second");
}

TEST_CASE("AcpPanel's C-RET steers the running turn", "[AcpPanel]") {
    Fixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession("claude-code", Json::object(), {{"_meta", {{"steering", {{"supported", true}}}}}});
    Type(fixture, "first");
    fixture.panel.OnEvent(ned::ui::test::Return());
    (void)fixture.reader.Next();

    Type(fixture, "nudge");
    REQUIRE(fixture.panel.OnEvent(ned::ui::test::ReturnCtrl()));
    REQUIRE(fixture.reader.Next()["method"] == "_session/steering");
    REQUIRE(fixture.manager.QueuedPrompts().empty());
}

TEST_CASE("AcpPanel's M-RET and S-RET start a new composer line that Enter sends along", "[AcpPanel]") {
    Fixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession("claude-code");
    Type(fixture, "one");
    REQUIRE(fixture.panel.OnEvent(ned::ui::test::ReturnAlt()));
    Type(fixture, "two");
    REQUIRE(fixture.panel.OnEvent(ned::ui::test::ReturnShift()));
    Type(fixture, "three");
    fixture.Paint();
    REQUIRE(fixture.RowText(kHeight - 3) == "Prompt: one");
    REQUIRE(fixture.RowText(kHeight - 2) == "two");
    REQUIRE(fixture.RowText(kHeight - 1) == "three");

    REQUIRE(fixture.panel.OnEvent(ned::ui::test::Return()));
    REQUIRE(fixture.reader.Next()["params"]["prompt"][0]["text"] == "one\ntwo\nthree");
}

TEST_CASE("AcpPanel's Up/Down move between composer lines before recalling history", "[AcpPanel]") {
    Fixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession("claude-code");
    fixture.manager.SendPrompt("earlier");
    Type(fixture, "abc");
    fixture.panel.OnEvent(ned::ui::test::ReturnAlt());
    Type(fixture, "xy");
    fixture.Paint();

    REQUIRE(fixture.panel.OnEvent(ned::ui::test::ArrowUp()));
    fixture.panel.OnEvent(ned::ui::test::Character('!'));
    fixture.Paint();
    REQUIRE(fixture.RowText(kHeight - 2) == "Prompt: !abc"); // column 2 of row 0 is inside the label

    REQUIRE(fixture.panel.OnEvent(ned::ui::test::End()));
    fixture.panel.OnEvent(ned::ui::test::Character('?'));
    fixture.Paint();
    REQUIRE(fixture.RowText(kHeight - 2) == "Prompt: !abc?");

    REQUIRE(fixture.panel.OnEvent(ned::ui::test::ArrowUp())); // first row: history
    fixture.Paint();
    REQUIRE(fixture.RowText(kHeight - 1) == "Prompt: earlier");
}

TEST_CASE("AcpPanel marks its title and notifies when a turn finishes while it isn't focused", "[AcpPanel]") {
    Fixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession("claude-code");
    std::vector<std::pair<std::string, std::string>> notified;
    fixture.panel.SetDesktopNotifier([&](const std::string& title, const std::string& body) { notified.emplace_back(title, body); });
    fixture.manager.SetOnAttention([&](ned::editor::acp::Manager::Attention attention, std::chrono::steady_clock::duration elapsed) {
        fixture.panel.NoteAttention(attention, elapsed);
    });

    fixture.manager.SendPrompt("go");
    const Json request = fixture.reader.Next();
    fixture.AgentSays("\nAll tests pass now.\nDetails follow.");
    fixture.client->DispatchFrame(ResultFrame(request["id"], Json{{"stopReason", "end_turn"}}));

    REQUIRE(fixture.panel.TitleText().find("● claude-code") == 0);
    REQUIRE(notified.size() == 1);
    REQUIRE(notified[0].first == "claude-code finished");
    REQUIRE(notified[0].second == "All tests pass now.");

    fixture.panel.OnEvent(ned::ui::test::Mouse(1, 1, ned::ui::MouseEvent::Button::Left, ned::ui::MouseEvent::Motion::Pressed));
    fixture.Paint();
    REQUIRE(fixture.panel.TitleText().find("●") == std::string::npos);
}

TEST_CASE("AcpPanel stays quiet about a short turn it was watching", "[AcpPanel]") {
    Fixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession("claude-code");
    int notified = 0;
    fixture.panel.SetDesktopNotifier([&](const std::string&, const std::string&) { ++notified; });
    fixture.panel.OnEvent(ned::ui::test::Mouse(1, 1, ned::ui::MouseEvent::Button::Left, ned::ui::MouseEvent::Motion::Pressed));
    REQUIRE(fixture.panel.Focused());

    fixture.panel.NoteAttention(ned::editor::acp::Manager::Attention::TurnFinished, std::chrono::seconds(2));
    REQUIRE(notified == 0);
    REQUIRE(fixture.panel.TitleText().find("●") == std::string::npos);

    fixture.panel.NoteAttention(ned::editor::acp::Manager::Attention::TurnFinished, std::chrono::seconds(45));
    REQUIRE(notified == 1);
}

TEST_CASE("AcpPanel's C-c ' continues the composer in a compose buffer whose send submits it", "[AcpPanel]") {
    Fixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession("claude-code");
    std::optional<ned::editor::acp::ComposeCallbacks> callbacks;
    std::string                                       seed;
    int                                               refocused = 0;
    fixture.panel.SetOnComposeRequest([&](std::string text, ned::editor::acp::ComposeCallbacks c) {
        seed      = std::move(text);
        callbacks = std::move(c);
    });
    fixture.panel.SetOnRefocusRequest([&] { ++refocused; });

    Type(fixture, "start");
    REQUIRE(fixture.panel.OnEvent(ned::ui::test::Ctrl('c')));
    REQUIRE(fixture.panel.OnEvent(ned::ui::test::Character('\'')));
    REQUIRE(seed == "start");
    REQUIRE(callbacks);

    callbacks->onSend("start\nand finish");
    REQUIRE(fixture.reader.Next()["params"]["prompt"][0]["text"] == "start\nand finish");
    REQUIRE(refocused == 1);
}

TEST_CASE("AcpPanel drops a C-c prefix that another key doesn't complete", "[AcpPanel]") {
    Fixture fixture;
    fixture.panel.OnEvent(ned::ui::test::Ctrl('c'));
    fixture.panel.OnEvent(ned::ui::test::Character('x'));
    fixture.Paint();
    REQUIRE(fixture.RowText(kHeight - 1) == "Prompt: x");
}

TEST_CASE("AcpPanel's C-c C-s steers too, for terminals without C-RET", "[AcpPanel]") {
    Fixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession("claude-code", Json::object(), {{"_meta", {{"steering", {{"supported", true}}}}}});
    Type(fixture, "first");
    fixture.panel.OnEvent(ned::ui::test::Return());
    (void)fixture.reader.Next();

    Type(fixture, "nudge");
    fixture.panel.OnEvent(ned::ui::test::Ctrl('c'));
    REQUIRE(fixture.panel.OnEvent(ned::ui::test::Ctrl('s')));
    REQUIRE(fixture.reader.Next()["method"] == "_session/steering");
}

TEST_CASE("AcpPanel copies a code block from its header and colours it by language", "[AcpPanel]") {
    Fixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession("claude-code");
    std::string copied;
    fixture.panel.SetOnCopy([&copied](const std::string& text) { copied = text; });
    fixture.AgentSays("```cpp\nint x = 1;\n```");
    fixture.Paint();

    int header = -1;
    int code   = -1;
    for (int y = 1; y < kHeight - 1; ++y) {
        if (fixture.RowText(y).find("⧉ copy") != std::string::npos) {
            header = y;
        }
        if (fixture.RowText(y) == "int x = 1;") {
            code = y;
        }
    }
    REQUIRE(header >= 0);
    REQUIRE(code >= 0);
    REQUIRE(fixture.screen.PixelAt(0, code).background_color == fixture.theme.documentHighlightBackground);
    REQUIRE(fixture.screen.PixelAt(0, code).foreground_color != fixture.theme.borderAccent.foreground);

    REQUIRE(fixture.panel.OnEvent(ned::ui::test::Mouse(1, header, ned::ui::MouseEvent::Button::Left, ned::ui::MouseEvent::Motion::Pressed)));
    REQUIRE(copied == "int x = 1;");
}

TEST_CASE("AcpPanel's M-w copies a reply picked from the transcript", "[AcpPanel]") {
    Fixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession("claude-code");
    std::string copied;
    fixture.panel.SetOnCopy([&copied](const std::string& text) { copied = text; });
    fixture.AgentSays("All done.");
    REQUIRE(fixture.panel.OnEvent(ned::ui::test::Alt('w')));
    fixture.Paint();
    REQUIRE(fixture.ContentText().find("Copy what?") != std::string::npos);
    REQUIRE(fixture.panel.OnEvent(ned::ui::test::Character('1')));
    REQUIRE(copied == "All done.");
    fixture.Paint();
    REQUIRE(fixture.ContentText().find("copied 1 line") != std::string::npos);
}

TEST_CASE("AcpPanel's C-c C-f toggles following the agent", "[AcpPanel]") {
    Fixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession("claude-code");
    REQUIRE_FALSE(ned::editor::acp::GetAcpFollowAgent());
    fixture.panel.OnEvent(ned::ui::test::Ctrl('c'));
    REQUIRE(fixture.panel.OnEvent(ned::ui::test::Ctrl('f')));
    REQUIRE(ned::editor::acp::GetAcpFollowAgent());
    fixture.Paint();
    REQUIRE(fixture.ContentText().find("⇢ follow") != std::string::npos);
    fixture.panel.OnEvent(ned::ui::test::Ctrl('c'));
    fixture.panel.OnEvent(ned::ui::test::Ctrl('f'));
    REQUIRE_FALSE(ned::editor::acp::GetAcpFollowAgent());
}

TEST_CASE("AcpPanel's C-v attaches a clipboard image to the next prompt", "[AcpPanel]") {
    Fixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession("claude-code", Json::object(), {{"agentCapabilities", {{"promptCapabilities", {{"image", true}}}}}});
    fixture.panel.SetClipboardSource([] { return std::optional<ned::editor::ClipboardImage>({.mimeType = "image/png", .bytes = "png!"}); },
                                     [] { return std::optional<std::string>(); });
    REQUIRE(fixture.panel.OnEvent(ned::ui::test::Ctrl('v')));
    REQUIRE(fixture.panel.OnEvent(ned::ui::test::Ctrl('v')));
    fixture.Paint();
    REQUIRE(fixture.RowText(kHeight - 2).starts_with("▣ image 1 (png, 1 KB) · image 2"));

    // Backspace at the composer's start takes the last one back.
    REQUIRE(fixture.panel.OnEvent(ned::ui::test::Backspace()));
    Type(fixture, "see");
    fixture.panel.OnEvent(ned::ui::test::Return());
    const Json prompt = fixture.reader.Next()["params"]["prompt"];
    REQUIRE(prompt.size() == 2);
    REQUIRE(prompt[0]["text"] == "see");
    REQUIRE(prompt[1]["type"] == "image");
    REQUIRE(prompt[1]["data"] == "cG5nIQ==");
    fixture.Paint();
    REQUIRE(fixture.ContentText().find("▣") == std::string::npos);
}

TEST_CASE("AcpPanel's C-v refuses an image the agent can't take, and pastes text otherwise", "[AcpPanel]") {
    Fixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession("claude-code");
    bool hasImage = true;
    fixture.panel.SetClipboardSource(
        [&hasImage] {
            return hasImage ? std::optional<ned::editor::ClipboardImage>({.mimeType = "image/png", .bytes = "png!"}) : std::nullopt;
        },
        [] { return std::optional<std::string>("pasted\r\ntext"); });
    fixture.panel.OnEvent(ned::ui::test::Ctrl('v'));
    fixture.Paint();
    REQUIRE(fixture.ContentText().find("this agent doesn't accept images") != std::string::npos);

    hasImage = false;
    fixture.panel.OnEvent(ned::ui::test::Ctrl('v'));
    fixture.panel.OnEvent(ned::ui::test::Return());
    REQUIRE(fixture.reader.Next()["params"]["prompt"][0]["text"] == "pasted\ntext");
}

TEST_CASE("AcpPanel hands C-c and C-x sequences it doesn't bind to the editor's keymap", "[AcpPanel]") {
    Fixture                            fixture;
    std::vector<ned::editor::KeyChord> forwarded;
    std::size_t                        sequenceLength = 0; // how many chords the fake keymap's sequence takes
    fixture.panel.SetOnForwardChord([&](const ned::editor::KeyChord& chord) {
        forwarded.push_back(chord);
        return forwarded.size() < sequenceLength;
    });

    sequenceLength = 3; // C-c A s
    fixture.panel.OnEvent(ned::ui::test::Ctrl('c'));
    fixture.panel.OnEvent(ned::ui::test::Character('A'));
    fixture.panel.OnEvent(ned::ui::test::Character('s'));
    REQUIRE(forwarded.size() == 3);
    REQUIRE(forwarded[0].Control);
    REQUIRE(forwarded[0].Codepoint == U'c');
    REQUIRE(forwarded[2].Codepoint == U's');

    forwarded.clear();
    sequenceLength = 2; // C-x o
    fixture.panel.OnEvent(ned::ui::test::Ctrl('x'));
    fixture.panel.OnEvent(ned::ui::test::Character('o'));
    REQUIRE(forwarded.size() == 2);

    // Once the sequence is done, keys type again.
    fixture.panel.OnEvent(ned::ui::test::Character('z'));
    fixture.Paint();
    REQUIRE(fixture.RowText(kHeight - 1) == "Prompt: z");
    REQUIRE(forwarded.size() == 2);
}

TEST_CASE("AcpPanel answers an agent's question from its form", "[AcpPanel]") {
    Fixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession("claude-code");
    const Json schema = {{"type", "object"},
                         {"properties",
                          {{"question_0",
                            {{"type", "string"}, {"oneOf", Json::array({Json{{"const", "A"}, {"title", "Alpha"}}, Json{{"const", "B"}, {"title", "Beta"}}})}}}}}};
    fixture.client->DispatchFrame(Json{{"jsonrpc", "2.0"},
                                       {"id", 40},
                                       {"method", "elicitation/create"},
                                       {"params", {{"sessionId", "s1"}, {"mode", "form"}, {"message", "Pick"}, {"requestedSchema", schema}}}}
                                      .dump());
    fixture.Paint();
    REQUIRE(fixture.RowText(1) == "? Pick");
    REQUIRE(fixture.ContentText().find("> 1 ○ Alpha") != std::string::npos);

    // The form owns the keyboard: nothing reaches the composer.
    REQUIRE(fixture.panel.OnEvent(ned::ui::test::Character('2')));
    const Json answer = fixture.reader.Next();
    REQUIRE(answer["id"] == 40);
    REQUIRE(answer["result"] == Json{{"action", "accept"}, {"content", {{"question_0", "B"}}}});
    fixture.Paint();
    REQUIRE(fixture.RowText(kHeight - 1) == "Prompt:");
    REQUIRE(fixture.ContentText().find("answered: Beta") != std::string::npos);
}

TEST_CASE("AcpPanel opens a question's URL and can decline one", "[AcpPanel]") {
    Fixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession("claude-code");
    std::string opened;
    fixture.panel.SetUrlOpener([&opened](const std::string& url) {
        opened = url;
        return true;
    });
    auto ask = [&fixture](int id, const Json& params) {
        fixture.client->DispatchFrame(Json{{"jsonrpc", "2.0"}, {"id", id}, {"method", "elicitation/create"}, {"params", params}}.dump());
    };
    ask(41, {{"sessionId", "s1"}, {"mode", "url"}, {"message", "Log in"}, {"url", "https://x.test/login"}, {"elicitationId", "e"}});
    fixture.panel.OnEvent(ned::ui::test::Return());
    REQUIRE(opened == "https://x.test/login");
    REQUIRE(fixture.reader.Next()["result"]["action"] == "accept");

    ask(42, {{"sessionId", "s1"}, {"mode", "form"}, {"message", "Why?"}, {"requestedSchema", {{"type", "object"}}}});
    fixture.panel.OnEvent(ned::ui::test::Escape());
    REQUIRE(fixture.reader.Next()["result"]["action"] == "decline");
}

TEST_CASE("AcpPanel's login picker runs a terminal login and carries on once it succeeds", "[AcpPanel]") {
    ned::editor::acp::SetAcpAgentCommand("login-agent", {"agent-bin"});
    Fixture fixture;
    fixture.InjectClient();
    fixture.manager.SetOnLoginRequired([&fixture] { fixture.panel.OpenPicker(ned::editor::acp::PanelPicker::Login); });
    std::vector<std::string>                         loginArgv;
    std::vector<std::pair<std::string, std::string>> loginEnv;
    std::string                                      loginLabel;
    std::function<void(bool)>                        loginDone;
    fixture.panel.SetOnTerminalLogin([&](std::vector<std::string> argv, std::vector<std::pair<std::string, std::string>> env, std::string label,
                                         std::function<void(bool)> done) {
        loginArgv  = std::move(argv);
        loginEnv   = std::move(env);
        loginLabel = std::move(label);
        loginDone  = std::move(done);
    });

    fixture.manager.StartSession("login-agent");
    const Json initialize = fixture.reader.Next();
    fixture.client->DispatchFrame(ResultFrame(
        initialize["id"], Json{{"authMethods", Json::array({Json{{"id", "console-login"},
                                                                 {"name", "Anthropic Console"},
                                                                 {"description", "API billing"},
                                                                 {"type", "terminal"},
                                                                 {"args", Json::array({"--cli", "auth", "login"})},
                                                                 {"env", {{"X", "1"}}}}})}}));
    const Json sessionNew = fixture.reader.Next();
    fixture.client->DispatchFrame(
        Json{{"jsonrpc", "2.0"}, {"id", sessionNew["id"]}, {"error", {{"code", -32000}, {"message", "Authentication required"}}}}.dump());
    fixture.Paint();
    REQUIRE(fixture.RowText(0).find("[login required]") != std::string::npos);
    REQUIRE(fixture.RowText(1) == "Log in how?");
    REQUIRE(fixture.RowText(2).find("Anthropic Console  API billing") != std::string::npos);

    REQUIRE(fixture.panel.OnEvent(ned::ui::test::Return()));
    REQUIRE(loginArgv == std::vector<std::string>{"agent-bin", "--cli", "auth", "login"});
    REQUIRE(loginEnv == std::vector<std::pair<std::string, std::string>>{{"X", "1"}});
    REQUIRE(loginLabel == "Anthropic Console");

    loginDone(true);
    const Json retry = fixture.reader.Next();
    REQUIRE(retry["method"] == "session/new");
    fixture.client->DispatchFrame(ResultFrame(retry["id"], Json{{"sessionId", "s1"}}));
    fixture.Paint();
    REQUIRE(fixture.RowText(0).find("[active]") != std::string::npos);
}

TEST_CASE("AcpPanel's login picker hands an agent method to authenticate", "[AcpPanel]") {
    Fixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession("claude-code", Json::object(), {{"authMethods", Json::array({Json{{"id", "gateway"}, {"name", "Gateway"}}})}});
    fixture.panel.OpenPicker(ned::editor::acp::PanelPicker::Login);
    REQUIRE(fixture.panel.OnEvent(ned::ui::test::Character('1')));
    const Json request = fixture.reader.Next();
    REQUIRE(request["method"] == "authenticate");
    REQUIRE(request["params"]["methodId"] == "gateway");
}

TEST_CASE("AcpPanel draws an agent's picture under its caption", "[AcpPanel][AcpImages]") {
    Fixture fixture;
    fixture.InjectClient();
    fixture.panel.SetEventLoop(&fixture.eventLoop);
    fixture.StartActiveSession("claude-code");
    // 3x2: red, green, blue over white, clear, dark blue.
    fixture.SendUpdate({{"sessionUpdate", "agent_message_chunk"},
                        {"content",
                         {{"type", "image"},
                          {"mimeType", "image/png"},
                          {"data", "iVBORw0KGgoAAAANSUhEUgAAAAMAAAACCAYAAACddGYaAAAAHUlEQVR4nAXBoQEAMAzAIHT1dG/t5xmIJEoVzNv7nJ4Ksxn7EooAAAAASUVORK5CYII="}}}});
    fixture.Paint();
    int caption = -1;
    for (int y = 0; y < kHeight; ++y) {
        if (fixture.RowText(y).starts_with("▣ image")) {
            caption = y;
        }
    }
    REQUIRE(caption >= 0);
    REQUIRE(caption + 1 < kHeight);
    const ned::ui::Cell& corner = fixture.screen.PixelAt(2, caption + 1);
    REQUIRE(corner.character != " ");
    auto red = [](const ned::ui::Color& color) {
        std::uint8_t r = 0, g = 0, b = 0;
        ned::ui::ColorToRgb8(color, r, g, b);
        return r > 150 && g < 100 && b < 100;
    };
    REQUIRE((red(corner.foreground_color) || red(corner.background_color)));
    fixture.panel.EndFrame();
}

namespace {

constexpr const char* kTinyPng = "iVBORw0KGgoAAAANSUhEUgAAAAMAAAACCAYAAACddGYaAAAAHUlEQVR4nAXBoQEAMAzAIHT1dG/t5xmIJEoVzNv7nJ4Ksxn7EooAAAAASUVORK5CYII=";

// A fresh, empty directory for saved pictures, gone again afterwards.
struct SaveDirectory {
    std::filesystem::path path = std::filesystem::temp_directory_path() / ("ned-acp-save-" + std::to_string(::getpid()));
    SaveDirectory() {
        std::filesystem::remove_all(path);
    }
    ~SaveDirectory() {
        std::filesystem::remove_all(path);
    }
    [[nodiscard]] std::vector<std::filesystem::path> Files() const {
        std::vector<std::filesystem::path> files;
        if (std::filesystem::exists(path)) {
            for (const auto& entry : std::filesystem::directory_iterator(path)) {
                files.push_back(entry.path());
            }
        }
        return files;
    }
};

std::string FileBytes(const std::filesystem::path& path) {
    std::ifstream      file(path, std::ios::binary);
    std::ostringstream bytes;
    bytes << file.rdbuf();
    return bytes.str();
}

} // namespace

TEST_CASE("AcpPanel's right-click on a picture saves or copies it", "[AcpPanel][AcpImages]") {
    Fixture fixture;
    fixture.InjectClient();
    fixture.panel.SetEventLoop(&fixture.eventLoop);
    SaveDirectory directory;
    fixture.panel.SetImageDirectory(directory.path);
    std::string copiedType;
    std::string copiedBytes;
    fixture.panel.SetImageCopier([&](std::string_view mimeType, std::string_view bytes) {
        copiedType  = mimeType;
        copiedBytes = bytes;
        return true;
    });
    std::string                              title;
    std::vector<ned::ui::AcpPanel::MenuItem> items;
    ned::ui::Point                           anchor{};
    fixture.panel.SetOnContextMenuRequest([&](std::string menuTitle, std::vector<ned::ui::AcpPanel::MenuItem> menuItems, ned::ui::Point at) {
        title  = std::move(menuTitle);
        items  = std::move(menuItems);
        anchor = at;
    });
    fixture.StartActiveSession("claude-code");
    fixture.AgentSays("Here it is.");
    fixture.SendUpdate({{"sessionUpdate", "agent_message_chunk"}, {"content", {{"type", "image"}, {"mimeType", "image/png"}, {"data", kTinyPng}}}});
    fixture.Paint();
    int caption = -1;
    int reply   = -1;
    for (int y = 0; y < kHeight; ++y) {
        if (fixture.RowText(y).starts_with("▣ image")) {
            caption = y;
        }
        if (fixture.RowText(y).starts_with("Here it is.")) {
            reply = y;
        }
    }
    REQUIRE(caption >= 0);
    REQUIRE(reply >= 0);

    REQUIRE_FALSE(fixture.panel.OnEvent(ned::ui::test::Mouse(3, reply, ned::ui::MouseEvent::Button::Right, ned::ui::MouseEvent::Motion::Pressed)));
    REQUIRE(items.empty());

    REQUIRE(fixture.panel.OnEvent(ned::ui::test::Mouse(3, caption + 1, ned::ui::MouseEvent::Button::Right, ned::ui::MouseEvent::Motion::Pressed)));
    REQUIRE(title == "Picture");
    REQUIRE(anchor.x == 3);
    REQUIRE(anchor.y == caption + 1);
    REQUIRE(items.size() == 2);
    REQUIRE(items[0].label == "Save to " + directory.path.filename().string());
    REQUIRE(items[1].label == "Copy Picture");

    const std::string png = ned::text::Base64Decode(kTinyPng).value();
    items[0].action();
    items[0].action();
    // Saved twice: two files, the second beside the first rather than over it.
    const std::vector<std::filesystem::path> files = directory.Files();
    REQUIRE(files.size() == 2);
    for (const std::filesystem::path& file : files) {
        REQUIRE(file.filename().string().starts_with("ned-image-"));
        REQUIRE(file.extension() == ".png");
        REQUIRE(FileBytes(file) == png);
    }
    fixture.Paint();
    REQUIRE(fixture.ContentText().find("saved ") != std::string::npos);

    items[1].action();
    REQUIRE(copiedType == "image/png");
    REQUIRE(copiedBytes == png);

    // The caption row offers the same picture.
    items.clear();
    REQUIRE(fixture.panel.OnEvent(ned::ui::test::Mouse(3, caption, ned::ui::MouseEvent::Button::Right, ned::ui::MouseEvent::Motion::Pressed)));
    REQUIRE(items.size() == 2);
    fixture.panel.EndFrame();
}

TEST_CASE("AcpPanel's C-c C-w saves a picture picked from the transcript", "[AcpPanel][AcpImages]") {
    Fixture fixture;
    fixture.InjectClient();
    SaveDirectory directory;
    fixture.panel.SetImageDirectory(directory.path);
    fixture.StartActiveSession("claude-code");
    fixture.panel.OnEvent(ned::ui::test::Ctrl('c'));
    REQUIRE(fixture.panel.OnEvent(ned::ui::test::Ctrl('w')));
    fixture.Paint();
    REQUIRE(fixture.ContentText().find("No pictures yet") != std::string::npos);
    fixture.panel.OnEvent(ned::ui::test::Escape());

    fixture.SendUpdate({{"sessionUpdate", "agent_message_chunk"}, {"content", {{"type", "image"}, {"mimeType", "image/png"}, {"data", kTinyPng}}}});
    fixture.panel.OnEvent(ned::ui::test::Ctrl('c'));
    REQUIRE(fixture.panel.OnEvent(ned::ui::test::Ctrl('w')));
    fixture.Paint();
    REQUIRE(fixture.ContentText().find("Save which picture?") != std::string::npos);
    REQUIRE(fixture.panel.OnEvent(ned::ui::test::Character('1')));
    const std::vector<std::filesystem::path> files = directory.Files();
    REQUIRE(files.size() == 1);
    REQUIRE(FileBytes(files[0]) == ned::text::Base64Decode(kTinyPng).value());
}

TEST_CASE("AcpPanel's session strip switches conversations, each keeping its own draft", "[AcpPanel]") {
    Fixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession("claude-code", Json::object(), Json{{"agentCapabilities", {{"sessionCapabilities", {{"fork", Json::object()}}}}}});
    fixture.Paint();
    const std::string composerAlone = fixture.RowText(kHeight - 1);
    REQUIRE(composerAlone.find(" 1 new") == std::string::npos);
    REQUIRE(fixture.panel.OnEvent(ned::ui::test::Character('a')));

    fixture.panel.OpenPicker(ned::editor::acp::PanelPicker::Fork);
    const Json fork = fixture.reader.Next();
    REQUIRE(fork["method"] == "session/fork");
    fixture.client->DispatchFrame(ResultFrame(fork["id"], Json{{"sessionId", "s2"}}));
    fixture.Paint();
    // The strip takes the bottom row; the composer moves up above it.
    REQUIRE(fixture.RowText(kHeight - 1).starts_with(" 1 new  2 new  ×  +"));
    REQUIRE_FALSE(fixture.RowText(kHeight - 2).ends_with("a"));
    REQUIRE(fixture.panel.OnEvent(ned::ui::test::Character('b')));
    fixture.Paint();
    REQUIRE(fixture.RowText(kHeight - 2).ends_with("b"));

    REQUIRE(fixture.panel.OnEvent(ned::ui::test::Mouse(1, kHeight - 1, ned::ui::MouseEvent::Button::Left, ned::ui::MouseEvent::Motion::Pressed)));
    fixture.Paint();
    REQUIRE(fixture.manager.SessionId() == "s1");
    REQUIRE(fixture.RowText(kHeight - 2).ends_with("a"));

    REQUIRE(fixture.panel.OnEvent(ned::ui::test::PageDownCtrl()));
    fixture.Paint();
    REQUIRE(fixture.manager.SessionId() == "s2");
    REQUIRE(fixture.RowText(kHeight - 2).ends_with("b"));
}

TEST_CASE("AcpPanel can put the session strip on top", "[AcpPanel]") {
    Fixture fixture;
    fixture.InjectClient();
    fixture.StartActiveSession("claude-code");
    ned::editor::acp::SetAcpSessionTabs("top");
    REQUIRE(fixture.manager.NewSession() == "Starting a new conversation.");
    fixture.Paint();
    ned::editor::acp::SetAcpSessionTabs("bottom");
    REQUIRE(fixture.RowText(1).starts_with(" 1 new  2 new  ×  +"));
}
