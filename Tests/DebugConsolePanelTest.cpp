//
// DebugConsolePanel (Source/UI/DebugConsolePanel.h) -- headless coverage
// over a real DapManager wired to a pipe-backed DapClient, mirroring
// AcpPanelTest.cpp's own ManagerFixture/DispatchFrame pattern and
// TerminalPanelTest's per-cell Screen::PixelAt painting convention. DAP
// shares LSP's Content-Length framing (unlike ACP's newline-delimited
// JSON), so the frame reader here matches DapManagerTest's own.
//

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>

#include <unistd.h>

#include "Editor/Dap/DapClient.h"
#include "Editor/Dap/DapConfig.h"
#include "Editor/Dap/DapManager.h"
#include "Editor/Lsp/Transport.h"
#include "TestEvents.h"
#include "UI/DebugConsolePanel.h"
#include "UI/EventLoop.h"
#include "UI/Widget.h"

namespace {

using ned::editor::dap::DapClient;
using ned::editor::dap::DapManager;
using ned::editor::dap::Json;
using ned::editor::dap::SetDapLaunchConfig;
using ned::editor::lsp::Transport;
using ned::ui::Box;
using ned::ui::Canvas;
using ned::ui::DebugConsolePanel;
using ned::ui::Screen;
using ned::ui::Theme;

constexpr int kWidth  = 40;
constexpr int kHeight = 6; // 1 title + 4 content + 1 input

// Mirrors DapManagerTest.cpp's own FrameReader exactly (duplicated rather
// than shared, matching this codebase's per-test-file fixture convention).
struct FrameReader {
    int         fd;
    std::string buffer;

    Json Next() {
        for (int i = 0; i < 16; ++i) {
            const auto headerEnd = buffer.find("\r\n\r\n");
            if (headerEnd != std::string::npos) {
                const std::string_view kPrefix   = "Content-Length: ";
                const auto             prefixPos = buffer.find(kPrefix);
                REQUIRE(prefixPos != std::string::npos);
                const std::size_t contentLength = std::stoul(buffer.substr(prefixPos + kPrefix.size()));
                if (buffer.size() >= headerEnd + 4 + contentLength) {
                    const std::string body = buffer.substr(headerEnd + 4, contentLength);
                    buffer.erase(0, headerEnd + 4 + contentLength);
                    return Json::parse(body);
                }
            }
            char          chunk[512];
            const ssize_t n = ::read(fd, chunk, sizeof(chunk));
            if (n <= 0) {
                break;
            }
            buffer.append(chunk, static_cast<std::size_t>(n));
        }
        FAIL("no complete frame available on fd");
        return Json::object();
    }
};

std::string ResponseFrame(int requestSeq, const std::string& command, bool success, Json body = Json::object()) {
    return Json{{"seq", 1000 + requestSeq}, {"type", "response"}, {"request_seq", requestSeq},
               {"command", command},       {"success", success}, {"body", std::move(body)}}
        .dump();
}

struct Fixture {
    ned::ui::EventLoop eventLoop;
    DapManager         manager{eventLoop};
    Theme              theme = ned::ui::DarkTheme();
    DebugConsolePanel  panel{theme};
    Screen             screen{kWidth, kHeight};

    int          adapterStdinRead   = -1;
    int          adapterStdoutWrite = -1;
    DapClient*   client             = nullptr;
    FrameReader  reader{-1};

    Fixture() {
        panel.SetDapManager(&manager);
        panel.SetBox_(Box{.x_min = 0, .x_max = kWidth - 1, .y_min = 0, .y_max = kHeight - 1});
    }

    void InjectClient() {
        int clientWritesHere[2];
        int clientReadsHere[2];
        REQUIRE(::pipe(clientWritesHere) == 0);
        REQUIRE(::pipe(clientReadsHere) == 0);
        adapterStdinRead   = clientWritesHere[0];
        adapterStdoutWrite = clientReadsHere[1];
        reader.fd          = adapterStdinRead;
        client             = &manager.SetClientForTesting(
            std::make_unique<DapClient>(Transport(clientReadsHere[0], clientWritesHere[1]), eventLoop));
    }

    void StartRunningSession(const std::string& language) {
        SetDapLaunchConfig(language, R"({"program": "./fake-program"})");
        manager.StartOrContinue(language);
        const Json initialize = reader.Next();
        client->DispatchFrame(ResponseFrame(initialize["seq"].get<int>(), "initialize", true));
        const Json launch = reader.Next();
        client->DispatchFrame(ResponseFrame(launch["seq"].get<int>(), "launch", true));
        REQUIRE(manager.State() == DapManager::SessionState::Running);
        SetDapLaunchConfig(language, "");
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
        if (adapterStdoutWrite >= 0) {
            ::close(adapterStdoutWrite);
        }
        if (adapterStdinRead >= 0) {
            ::close(adapterStdinRead);
        }
    }
};

} // namespace

TEST_CASE("DebugConsolePanel's title row shows the session state", "[DebugConsolePanel]") {
    Fixture fixture;
    fixture.Paint();
    REQUIRE(fixture.RowText(0).find("[inactive]") != std::string::npos);

    fixture.InjectClient();
    fixture.StartRunningSession("debug-console-test-title");
    fixture.Paint();
    REQUIRE(fixture.RowText(0).find("[running]") != std::string::npos);
}

TEST_CASE("DebugConsolePanel's input row shows typed text and a caret", "[DebugConsolePanel]") {
    Fixture fixture;

    REQUIRE(fixture.panel.OnEvent(ned::ui::test::Character('h')));
    REQUIRE(fixture.panel.OnEvent(ned::ui::test::Character('i')));
    fixture.Paint();

    const std::string inputRow = fixture.RowText(kHeight - 1);
    REQUIRE(inputRow.find("debug> hi") != std::string::npos);
    REQUIRE(fixture.screen.PixelAt(static_cast<int>(std::string("debug> hi").size()), kHeight - 1).inverted);
}

TEST_CASE("DebugConsolePanel's Backspace deletes the last typed character", "[DebugConsolePanel]") {
    Fixture fixture;
    fixture.panel.OnEvent(ned::ui::test::Character('h'));
    fixture.panel.OnEvent(ned::ui::test::Character('i'));
    REQUIRE(fixture.panel.OnEvent(ned::ui::test::Backspace()));
    fixture.Paint();

    REQUIRE(fixture.RowText(kHeight - 1).find("debug> h") != std::string::npos);
    REQUIRE(fixture.RowText(kHeight - 1).find("debug> hi") == std::string::npos);
}

TEST_CASE("DebugConsolePanel's Enter with no active session shows Evaluate's own error and clears the input",
         "[DebugConsolePanel]") {
    Fixture fixture; // dapManager_ is set (constructor's SetDapManager), but no session was ever started
    fixture.panel.OnEvent(ned::ui::test::Character('x'));
    REQUIRE(fixture.panel.OnEvent(ned::ui::test::Return()));
    fixture.Paint();

    REQUIRE(fixture.RowText(kHeight - 1) == "debug>");
    bool foundError = false;
    for (int y = 1; y < kHeight - 1; ++y) {
        if (fixture.RowText(y).find("No debug session.") != std::string::npos) {
            foundError = true;
        }
    }
    REQUIRE(foundError);
}

TEST_CASE("DebugConsolePanel's Enter with no DapManager at all shows its own error line", "[DebugConsolePanel]") {
    Theme             theme = ned::ui::DarkTheme();
    DebugConsolePanel panel{theme}; // SetDapManager deliberately never called
    Screen            screen{kWidth, kHeight};
    panel.SetBox_(Box{.x_min = 0, .x_max = kWidth - 1, .y_min = 0, .y_max = kHeight - 1});

    panel.OnEvent(ned::ui::test::Character('x'));
    REQUIRE(panel.OnEvent(ned::ui::test::Return()));
    panel.Paint(Canvas(screen, panel.Box_()));

    bool foundError = false;
    for (int y = 1; y < kHeight - 1; ++y) {
        std::string row;
        for (int x = 0; x < kWidth; ++x) {
            row += screen.PixelAt(x, y).character;
        }
        if (row.find("No debugger available.") != std::string::npos) {
            foundError = true;
        }
    }
    REQUIRE(foundError);
}

TEST_CASE("DebugConsolePanel's Enter sends the typed expression through DapManager::Evaluate with repl context",
         "[DebugConsolePanel]") {
    Fixture fixture;
    fixture.InjectClient();
    fixture.StartRunningSession("debug-console-test-evaluate");
    fixture.client->DispatchFrame(Json{{"seq", 999}, {"type", "event"}, {"event", "stopped"}, {"body", {{"threadId", 1}}}}.dump());
    const Json autoStackTrace = fixture.reader.Next();
    fixture.client->DispatchFrame(ResponseFrame(autoStackTrace["seq"].get<int>(), "stackTrace", true, Json{{"stackFrames", Json::array()}}));

    fixture.panel.OnEvent(ned::ui::test::Character('x'));
    REQUIRE(fixture.panel.OnEvent(ned::ui::test::Return()));

    const Json evaluate = fixture.reader.Next();
    REQUIRE(evaluate["command"] == "evaluate");
    REQUIRE(evaluate["arguments"]["expression"] == "x");
    REQUIRE(evaluate["arguments"]["context"] == "repl");
    fixture.client->DispatchFrame(ResponseFrame(evaluate["seq"].get<int>(), "evaluate", true, Json{{"result", "42"}}));

    fixture.Paint();
    REQUIRE(fixture.RowText(kHeight - 1) == "debug>");
    bool foundInput  = false;
    bool foundResult = false;
    for (int y = 1; y < kHeight - 1; ++y) {
        const std::string row = fixture.RowText(y);
        if (row.find("> x") != std::string::npos) {
            foundInput = true;
        }
        if (row.find("42") != std::string::npos) {
            foundResult = true;
        }
    }
    REQUIRE(foundInput);
    REQUIRE(foundResult);
}

TEST_CASE("DebugConsolePanel's Escape and its close (x) button both invoke the toggle callback", "[DebugConsolePanel]") {
    Fixture fixture;
    int     toggles = 0;
    fixture.panel.SetOnToggleRequest([&toggles] { ++toggles; });

    REQUIRE(fixture.panel.OnEvent(ned::ui::test::Escape()));
    REQUIRE(toggles == 1);

    fixture.panel.OnEvent(
        ned::ui::test::Mouse(kWidth - 3, 0, ned::ui::MouseEvent::Button::Left, ned::ui::MouseEvent::Motion::Pressed));
    REQUIRE(toggles == 2);
}
