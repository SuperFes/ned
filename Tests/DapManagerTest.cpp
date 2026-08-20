#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <unistd.h>

#include "Editor/Dap/DapClient.h"
#include "Editor/Dap/DapConfig.h"
#include "Editor/Dap/DapManager.h"
#include "Editor/Lsp/Transport.h"
#include "UI/EventLoop.h"

using ned::editor::dap::DapClient;
using ned::editor::dap::DapManager;
using ned::editor::dap::Json;
using ned::editor::dap::SetDapLaunchConfig;
using ned::editor::lsp::Transport;

namespace {

// Same buffered frame reader as DapClientTest's -- DAP flows legitimately
// write two frames back to back (setBreakpoints + configurationDone), so
// leftover bytes must survive between calls.
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

std::string ResponseFrame(int requestSeq, const std::string& command, bool success, Json body = Json::object(),
                          const std::string& message = "") {
    Json response = {
        {"seq", 1000 + requestSeq},
        {"type", "response"},
        {"request_seq", requestSeq},
        {"command", command},
        {"success", success},
    };
    if (success) {
        response["body"] = std::move(body);
    }
    else {
        response["message"] = message;
    }
    return response.dump();
}

std::string EventFrame(const std::string& event, Json body = Json::object()) {
    return Json{{"seq", 999}, {"type", "event"}, {"event", event}, {"body", std::move(body)}}.dump();
}

// A DapManager plus a pipe-backed injected DapClient the test drives
// directly -- mirrors LspManagerTest's SetClientForTesting approach and
// DapClientTest's ClientFixture fd/lifetime discipline (write end closed in
// the destructor BODY, before members -- and thus the client's read thread's
// pipe -- are destroyed).
struct ManagerFixture {
    ned::ui::EventLoop eventLoop;
    DapManager         manager{eventLoop};
    int                adapterStdinRead   = -1;
    int                adapterStdoutWrite = -1;
    DapClient*         client             = nullptr;
    FrameReader        reader{-1};

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

    // Runs StartOrContinue through the initialize/launch handshake against
    // the fake adapter, leaving the session Running.
    void StartRunningSession(const std::string& language) {
        SetDapLaunchConfig(language, R"({"program": "./fake-program"})");
        REQUIRE(manager.StartOrContinue(language) == "Starting debug session (" + language + ")...");

        const Json initialize = reader.Next();
        REQUIRE(initialize["command"] == "initialize");
        client->DispatchFrame(ResponseFrame(initialize["seq"].get<int>(), "initialize", true));

        const Json launch = reader.Next();
        REQUIRE(launch["command"] == "launch");
        REQUIRE(launch["arguments"]["program"] == "./fake-program");
        client->DispatchFrame(ResponseFrame(launch["seq"].get<int>(), "launch", true));
        REQUIRE(manager.State() == DapManager::SessionState::Running);
    }

    ~ManagerFixture() {
        if (adapterStdoutWrite >= 0) {
            ::close(adapterStdoutWrite);
        }
        if (adapterStdinRead >= 0) {
            ::close(adapterStdinRead);
        }
    }
};

} // namespace

TEST_CASE("ToggleBreakpoint sets, sorts, and removes breakpoints per normalized file", "[Dap]") {
    ned::ui::EventLoop eventLoop;
    DapManager         manager(eventLoop);

    const std::filesystem::path path = std::filesystem::current_path() / "dap-test-file.c";
    REQUIRE(manager.ToggleBreakpoint(path, 12));
    REQUIRE(manager.ToggleBreakpoint(path, 3));
    REQUIRE(manager.BreakpointsForFile(path) == std::vector<std::size_t>{3, 12});
    // The same file via a different spelling lands on the same entry.
    REQUIRE(manager.BreakpointsForFile(std::filesystem::current_path() / "." / "dap-test-file.c") ==
            std::vector<std::size_t>{3, 12});

    REQUIRE_FALSE(manager.ToggleBreakpoint(path, 12)); // toggled off
    REQUIRE(manager.BreakpointsForFile(path) == std::vector<std::size_t>{3});
    REQUIRE_FALSE(manager.ToggleBreakpoint(path, 3));
    REQUIRE(manager.BreakpointsForFile(path).empty());
}

TEST_CASE("StartOrContinue refuses to start without a launch configuration", "[Dap]") {
    ned::ui::EventLoop eventLoop;
    DapManager         manager(eventLoop);

    const std::string status = manager.StartOrContinue("dap-manager-test-unconfigured");
    REQUIRE(status == "No launch configuration for dap-manager-test-unconfigured (ned/set-dap-launch).");
    REQUIRE(manager.State() == DapManager::SessionState::Inactive);
}

TEST_CASE("StartOrContinue runs the initialize/launch handshake and reaches Running", "[Dap]") {
    ManagerFixture fixture;
    fixture.InjectClient();
    fixture.StartRunningSession("dap-manager-test-handshake");
    SetDapLaunchConfig("dap-manager-test-handshake", "");
}

TEST_CASE("The initialized event pushes configured breakpoints then configurationDone", "[Dap]") {
    ManagerFixture fixture;
    fixture.InjectClient();

    const std::filesystem::path path = std::filesystem::current_path() / "dap-test-main.c";
    fixture.manager.ToggleBreakpoint(path, 7);
    fixture.manager.ToggleBreakpoint(path, 2);

    fixture.StartRunningSession("dap-manager-test-breakpoints");
    fixture.client->DispatchFrame(EventFrame("initialized"));

    const Json setBreakpoints = fixture.reader.Next();
    REQUIRE(setBreakpoints["command"] == "setBreakpoints");
    REQUIRE(setBreakpoints["arguments"]["source"]["path"].get<std::string>().ends_with("dap-test-main.c"));
    REQUIRE(setBreakpoints["arguments"]["breakpoints"] ==
            Json::array({Json{{"line", 2}}, Json{{"line", 7}}})); // sorted

    const Json configurationDone = fixture.reader.Next();
    REQUIRE(configurationDone["command"] == "configurationDone");
    SetDapLaunchConfig("dap-manager-test-breakpoints", "");
}

TEST_CASE("Toggling a breakpoint mid-session pushes setBreakpoints immediately", "[Dap]") {
    ManagerFixture fixture;
    fixture.InjectClient();
    fixture.StartRunningSession("dap-manager-test-live-toggle");

    const std::filesystem::path path = std::filesystem::current_path() / "dap-test-live.c";
    fixture.manager.ToggleBreakpoint(path, 5);

    const Json setBreakpoints = fixture.reader.Next();
    REQUIRE(setBreakpoints["command"] == "setBreakpoints");
    REQUIRE(setBreakpoints["arguments"]["breakpoints"] == Json::array({Json{{"line", 5}}}));

    // Removing the file's last breakpoint still reaches the adapter (an
    // empty list), rather than silently leaving the stale one armed.
    fixture.manager.ToggleBreakpoint(path, 5);
    const Json cleared = fixture.reader.Next();
    REQUIRE(cleared["command"] == "setBreakpoints");
    REQUIRE(cleared["arguments"]["breakpoints"] == Json::array());
    SetDapLaunchConfig("dap-manager-test-live-toggle", "");
}

TEST_CASE("A stopped event fetches the top stack frame and reports its source location", "[Dap]") {
    ManagerFixture fixture;
    fixture.InjectClient();
    fixture.StartRunningSession("dap-manager-test-stopped");

    DapManager::StoppedInfo stopped;
    bool                    stoppedFired = false;
    fixture.manager.SetOnStopped([&](const DapManager::StoppedInfo& info) {
        stopped      = info;
        stoppedFired = true;
    });

    fixture.client->DispatchFrame(EventFrame("stopped", Json{{"reason", "breakpoint"}, {"threadId", 4}}));
    REQUIRE(fixture.manager.State() == DapManager::SessionState::Stopped);

    const Json stackTrace = fixture.reader.Next();
    REQUIRE(stackTrace["command"] == "stackTrace");
    REQUIRE(stackTrace["arguments"]["threadId"] == 4);
    fixture.client->DispatchFrame(ResponseFrame(
        stackTrace["seq"].get<int>(), "stackTrace", true,
        Json{{"stackFrames", Json::array({Json{{"id", 1},
                                               {"name", "main"},
                                               {"line", 42},
                                               {"column", 1},
                                               {"source", Json{{"path", "/tmp/dap-test-stop.c"}}}}})}}));

    REQUIRE(stoppedFired);
    REQUIRE(stopped.reason == "breakpoint");
    REQUIRE(stopped.path.has_value());
    REQUIRE(*stopped.path == std::filesystem::path("/tmp/dap-test-stop.c"));
    REQUIRE(stopped.line == 42);

    // continue resumes the stopped thread.
    REQUIRE(fixture.manager.StartOrContinue("dap-manager-test-stopped") == "Continuing.");
    const Json continueRequest = fixture.reader.Next();
    REQUIRE(continueRequest["command"] == "continue");
    REQUIRE(continueRequest["arguments"]["threadId"] == 4);
    fixture.client->DispatchFrame(ResponseFrame(continueRequest["seq"].get<int>(), "continue", true));
    REQUIRE(fixture.manager.State() == DapManager::SessionState::Running);
    SetDapLaunchConfig("dap-manager-test-stopped", "");
}

TEST_CASE("A terminated event ends the session and reports it", "[Dap]") {
    ManagerFixture fixture;
    fixture.InjectClient();
    fixture.StartRunningSession("dap-manager-test-terminated");

    std::string endedReason;
    fixture.manager.SetOnSessionEnded([&](std::string reason) { endedReason = std::move(reason); });

    fixture.client->DispatchFrame(EventFrame("terminated"));
    REQUIRE(fixture.manager.State() == DapManager::SessionState::Inactive);
    REQUIRE(endedReason == "Debug session terminated.");
    SetDapLaunchConfig("dap-manager-test-terminated", "");
}

TEST_CASE("StopSession sends a disconnect and tears down immediately", "[Dap]") {
    ManagerFixture fixture;
    fixture.InjectClient();
    fixture.StartRunningSession("dap-manager-test-stop");

    std::string endedReason;
    fixture.manager.SetOnSessionEnded([&](std::string reason) { endedReason = std::move(reason); });

    REQUIRE(fixture.manager.StopSession() == "Debug session stopped.");
    REQUIRE(fixture.manager.State() == DapManager::SessionState::Inactive);
    REQUIRE(endedReason == "Debug session stopped.");

    const Json disconnect = fixture.reader.Next();
    REQUIRE(disconnect["command"] == "disconnect");
    REQUIRE(disconnect["arguments"]["terminateDebuggee"] == true);
    SetDapLaunchConfig("dap-manager-test-stop", "");
}
