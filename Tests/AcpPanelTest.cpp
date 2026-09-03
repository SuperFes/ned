//
// AcpPanel (Source/UI/AcpPanel.h) -- headless coverage over a real
// AcpManager wired to a pipe-backed AcpClient, the same ManagerFixture/
// DispatchFrame pattern AcpManagerTest.cpp uses, plus TerminalPanelTest's
// own per-cell Screen::PixelAt painting convention.
//

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include <unistd.h>

#include "Editor/Acp/AcpClient.h"
#include "Editor/Acp/AcpManager.h"
#include "Editor/Acp/AcpPanelConfig.h"
#include "Editor/Acp/Transport.h"
#include "Editor/ProjectRoot.h"
#include "TestEvents.h"
#include "Text/BufferList.h"
#include "UI/AcpPanel.h"
#include "UI/EventLoop.h"
#include "UI/Widget.h"

namespace {

using ned::editor::acp::AcpClient;
using ned::editor::acp::AcpManager;
using ned::editor::acp::Json;
using ned::editor::acp::Transport;
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
    AcpManager            manager{bufferList, eventLoop};
    Theme                 theme = ned::ui::DarkTheme();
    AcpPanel              panel{theme};
    Screen                screen{kWidth, kHeight};

    int           agentStdinRead   = -1;
    int           agentStdoutWrite = -1;
    AcpClient*    client           = nullptr;
    MessageReader reader{-1};

    Fixture() {
        panel.SetAcpManager(&manager);
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
            std::make_unique<AcpClient>(Transport(clientReadsHere[0], clientWritesHere[1]), eventLoop));
    }

    void StartActiveSession(const std::string& agentName) {
        manager.StartSession(agentName);
        const Json initializeRequest = reader.Next();
        client->DispatchFrame(ResultFrame(initializeRequest["id"], Json::object()));
        const Json sessionNewRequest = reader.Next();
        client->DispatchFrame(ResultFrame(sessionNewRequest["id"], Json{{"sessionId", "s1"}}));
        REQUIRE(manager.State() == AcpManager::SessionState::Active);
    }

    void Paint() {
        panel.Paint(Canvas(screen, panel.Box_()));
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
    REQUIRE(caretCell.foreground_color == ned::ui::Color::Black);
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
    REQUIRE(caretCell.foreground_color == ned::ui::Color::Black);
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

TEST_CASE("AcpPanel's Enter sends the typed prompt through AcpManager and clears the input row", "[AcpPanel]") {
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
            if (cell.background_color == fixture.theme.echoArea.foreground && cell.foreground_color == ned::ui::Color::Black) {
                foundCaret = true;
            }
        }
    }
    REQUIRE(foundCaret); // caret still rendered somewhere once the composer spans multiple rows
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

TEST_CASE("AcpPanel's Control-Up/Down grow/shrink AcpPanelSizePercent", "[AcpPanel]") {
    const int original = ned::editor::acp::AcpPanelSizePercent();
    ned::editor::acp::SetAcpPanelSizePercent(30);

    Fixture fixture;
    REQUIRE(fixture.panel.OnEvent(ned::ui::test::ArrowUpCtrl()));
    REQUIRE(ned::editor::acp::AcpPanelSizePercent() == 35);

    REQUIRE(fixture.panel.OnEvent(ned::ui::test::ArrowDownCtrl()));
    REQUIRE(fixture.panel.OnEvent(ned::ui::test::ArrowDownCtrl()));
    REQUIRE(ned::editor::acp::AcpPanelSizePercent() == 25);

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
