//
// DapThreadsPanel (Source/UI/DapThreadsPanel.h) -- headless coverage over a
// real DapManager wired to a pipe-backed DapClient, mirroring
// DebugConsolePanelTest.cpp's own Fixture/FrameReader pattern (DAP shares
// LSP's Content-Length framing, so the frame reader matches DapManagerTest's
// own).
//

#include <catch2/catch_test_macros.hpp>

#include <string>

#include <unistd.h>

#include "Editor/Dap/DapClient.h"
#include "Editor/Dap/DapConfig.h"
#include "Editor/Dap/DapManager.h"
#include "Editor/Lsp/Transport.h"
#include "TestEvents.h"
#include "UI/DapThreadsPanel.h"
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
using ned::ui::DapThreadsPanel;
using ned::ui::Screen;
using ned::ui::Theme;

constexpr int kWidth  = 40;
constexpr int kHeight = 8;

// Mirrors DapManagerTest.cpp's/DebugConsolePanelTest.cpp's own FrameReader
// exactly (duplicated rather than shared, matching this codebase's
// per-test-file fixture convention).
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
    return Json{{"seq", 1000 + requestSeq}, {"type", "response"}, {"request_seq", requestSeq}, {"command", command}, {"success", success}, {"body", std::move(body)}}
        .dump();
}

std::string EventFrame(const std::string& event, Json body = Json::object()) {
    return Json{{"seq", 2000}, {"type", "event"}, {"event", event}, {"body", std::move(body)}}.dump();
}

struct Fixture {
    ned::ui::EventLoop eventLoop;
    DapManager         manager{eventLoop};
    Theme              theme = ned::ui::DarkTheme();
    DapThreadsPanel    panel{theme, manager};
    Screen             screen{kWidth, kHeight};

    int         adapterStdinRead   = -1;
    int         adapterStdoutWrite = -1;
    DapClient*  client             = nullptr;
    FrameReader reader{-1};

    Fixture() {
        panel.Popup().SetBox_(Box{.x_min = 0, .x_max = kWidth - 1, .y_min = 0, .y_max = kHeight - 1});
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

    // Stops at threadId, answering the stackTrace request HandleStoppedEvent
    // fires with an empty frame list (path/line aren't under test here).
    void StopAt(int threadId) {
        client->DispatchFrame(EventFrame("stopped", Json{{"reason", "breakpoint"}, {"threadId", threadId}}));
        const Json stackTrace = reader.Next();
        client->DispatchFrame(ResponseFrame(stackTrace["seq"].get<int>(), "stackTrace", true, Json{{"stackFrames", Json::array()}}));
        REQUIRE(manager.State() == DapManager::SessionState::Stopped);
    }

    // Answers panel.Show()/Refresh()'s own "threads" request with a fixed
    // two-thread list -- StopAt(1) makes thread 1 the current one.
    void AnswerThreadsRequest() {
        const Json request = reader.Next();
        REQUIRE(request["command"] == "threads");
        client->DispatchFrame(ResponseFrame(
            request["seq"].get<int>(), "threads", true,
            Json{{"threads", Json::array({Json{{"id", 1}, {"name", "main"}}, Json{{"id", 2}, {"name", "worker"}}})}}));
    }

    void Paint() {
        panel.Popup().Paint(Canvas(screen, panel.Popup().Box_()));
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

TEST_CASE("DapThreadsPanel Show lists every thread and marks the current one", "[DapThreadsPanel]") {
    Fixture fixture;
    fixture.InjectClient();
    fixture.StartRunningSession("dap-threads-panel-test-list");
    fixture.StopAt(1);

    fixture.panel.Show();
    fixture.AnswerThreadsRequest();
    fixture.Paint();

    REQUIRE(fixture.RowText(1).find("main") != std::string::npos);
    REQUIRE(fixture.RowText(1).find("#1") != std::string::npos);
    REQUIRE(fixture.RowText(1).find("\xe2\x86\x92") != std::string::npos); // -> marks thread 1, the current one
    REQUIRE(fixture.RowText(2).find("worker") != std::string::npos);
    REQUIRE(fixture.RowText(2).find("#2") != std::string::npos);
    REQUIRE(fixture.RowText(2).find("\xe2\x86\x92") == std::string::npos);

    SetDapLaunchConfig("dap-threads-panel-test-list", "");
}

TEST_CASE("DapThreadsPanel Enter selects the highlighted row's thread", "[DapThreadsPanel]") {
    Fixture fixture;
    fixture.InjectClient();
    fixture.StartRunningSession("dap-threads-panel-test-select");
    fixture.StopAt(1);

    fixture.panel.Show();
    fixture.AnswerThreadsRequest();
    fixture.panel.Popup().TakeFocus();

    std::string message;
    fixture.panel.SetOnMessage([&](std::string m) { message = std::move(m); });

    // Move the highlight onto row 1 (thread 2, "worker") then activate it.
    REQUIRE(fixture.panel.Popup().OnEvent(ned::ui::test::ArrowDown()));
    REQUIRE(fixture.panel.Popup().OnEvent(ned::ui::test::Return()));

    const Json selectStackTrace = fixture.reader.Next();
    REQUIRE(selectStackTrace["command"] == "stackTrace");
    REQUIRE(selectStackTrace["arguments"]["threadId"] == 2);
    fixture.client->DispatchFrame(ResponseFrame(selectStackTrace["seq"].get<int>(), "stackTrace", true, Json{{"stackFrames", Json::array()}}));

    REQUIRE(message == "Selected thread: worker");

    fixture.Paint();
    REQUIRE(fixture.RowText(2).find("\xe2\x86\x92") != std::string::npos); // -> moved to thread 2's row

    SetDapLaunchConfig("dap-threads-panel-test-select", "");
}

TEST_CASE("DapThreadsPanel 'g' re-fetches threads", "[DapThreadsPanel]") {
    Fixture fixture;
    fixture.InjectClient();
    fixture.StartRunningSession("dap-threads-panel-test-refresh");
    fixture.StopAt(1);

    fixture.panel.Show();
    fixture.AnswerThreadsRequest();
    fixture.panel.Popup().TakeFocus();

    REQUIRE(fixture.panel.Popup().OnEvent(ned::ui::test::Character('g')));
    fixture.AnswerThreadsRequest(); // 'g' fires a second "threads" request
}

TEST_CASE("DapThreadsPanel Escape fires the cancel handler", "[DapThreadsPanel]") {
    Fixture fixture;
    bool    cancelled = false;
    fixture.panel.SetOnCancel([&] { cancelled = true; });
    fixture.panel.Popup().TakeFocus();

    REQUIRE(fixture.panel.Popup().OnEvent(ned::ui::test::Escape()));
    REQUIRE(cancelled);
}

TEST_CASE("DapThreadsPanel Refresh with no session reports an empty list", "[DapThreadsPanel]") {
    Fixture fixture; // no InjectClient()/session at all
    fixture.panel.Show();
    fixture.Paint();

    // No rows at all -- row 1 is just the left/right border columns around
    // blank interior, not empty outright (Popup()'s own bordered-box shape).
    REQUIRE(fixture.RowText(1).find("main") == std::string::npos);
    REQUIRE(fixture.RowText(1).find('#') == std::string::npos);
}
