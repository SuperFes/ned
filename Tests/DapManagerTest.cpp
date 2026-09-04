#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <memory>
#include <set>
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
using ned::editor::dap::SetDapAttachConfig;
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

    // DAP round 3: StartRunningSession's own shape, but through
    // DapManager::Attach -- an "attach" request instead of "launch".
    void StartAttachedSession(const std::string& language) {
        SetDapAttachConfig(language, R"({"processId": 4242})");
        REQUIRE(manager.Attach(language) == "Starting debug session (" + language + ")...");

        const Json initialize = reader.Next();
        REQUIRE(initialize["command"] == "initialize");
        client->DispatchFrame(ResponseFrame(initialize["seq"].get<int>(), "initialize", true));

        const Json attach = reader.Next();
        REQUIRE(attach["command"] == "attach");
        REQUIRE(attach["arguments"]["processId"] == 4242);
        client->DispatchFrame(ResponseFrame(attach["seq"].get<int>(), "attach", true));
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

    // DAP round 3: setFunctionBreakpoints/setExceptionBreakpoints go out
    // next, empty here (nothing registered), before configurationDone.
    const Json setFunctionBreakpoints = fixture.reader.Next();
    REQUIRE(setFunctionBreakpoints["command"] == "setFunctionBreakpoints");
    REQUIRE(setFunctionBreakpoints["arguments"]["breakpoints"] == Json::array());

    const Json setExceptionBreakpoints = fixture.reader.Next();
    REQUIRE(setExceptionBreakpoints["command"] == "setExceptionBreakpoints");
    REQUIRE(setExceptionBreakpoints["arguments"]["filters"] == Json::array());

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

TEST_CASE("A stop records the normalized location and stepping clears it while resuming", "[Dap]") {
    ManagerFixture fixture;
    fixture.InjectClient();
    fixture.StartRunningSession("dap-manager-test-step");

    fixture.client->DispatchFrame(EventFrame("stopped", Json{{"reason", "breakpoint"}, {"threadId", 2}}));
    const Json stackTrace = fixture.reader.Next();
    fixture.client->DispatchFrame(ResponseFrame(
        stackTrace["seq"].get<int>(), "stackTrace", true,
        Json{{"stackFrames", Json::array({Json{{"id", 9},
                                               {"name", "main"},
                                               {"line", 7},
                                               {"source", Json{{"path", "/tmp/dap-step-test.c"}}}}})}}));

    const auto stop = fixture.manager.CurrentStopKeyAndLine();
    REQUIRE(stop.has_value());
    REQUIRE(stop->first == DapManager::NormalizePathKey("/tmp/dap-step-test.c"));
    REQUIRE(stop->second == 7);

    REQUIRE(fixture.manager.StepOver() == "Stepping over...");
    const Json next = fixture.reader.Next();
    REQUIRE(next["command"] == "next");
    REQUIRE(next["arguments"]["threadId"] == 2);
    fixture.client->DispatchFrame(ResponseFrame(next["seq"].get<int>(), "next", true));
    REQUIRE(fixture.manager.State() == DapManager::SessionState::Running);
    REQUIRE_FALSE(fixture.manager.CurrentStopKeyAndLine().has_value());

    // Stepping while running is refused outright.
    REQUIRE(fixture.manager.StepInto() == "Not stopped (nothing to step).");
    SetDapLaunchConfig("dap-manager-test-step", "");
}

TEST_CASE("RunToCursor refuses when the session is not stopped", "[Dap]") {
    ManagerFixture fixture;
    fixture.InjectClient();
    fixture.StartRunningSession("dap-manager-test-run-to-cursor-refuse");

    const std::filesystem::path path = std::filesystem::current_path() / "dap-run-to-cursor-refuse.c";
    REQUIRE(fixture.manager.RunToCursor(path, 5) == "Not stopped (nothing to run to cursor from).");
    REQUIRE(fixture.manager.BreakpointsForFile(path).empty());
    SetDapLaunchConfig("dap-manager-test-run-to-cursor-refuse", "");
}

TEST_CASE("RunToCursor sets a temporary breakpoint, continues, and clears it on the next stop", "[Dap]") {
    ManagerFixture fixture;
    fixture.InjectClient();
    fixture.StartRunningSession("dap-manager-test-run-to-cursor");

    const std::filesystem::path path = std::filesystem::current_path() / "dap-run-to-cursor.c";

    // Get to Stopped first, on an unrelated line -- run-to-cursor's own
    // gating requires an already-stopped session.
    fixture.client->DispatchFrame(EventFrame("stopped", Json{{"reason", "breakpoint"}, {"threadId", 3}}));
    const Json firstStackTrace = fixture.reader.Next();
    fixture.client->DispatchFrame(ResponseFrame(
        firstStackTrace["seq"].get<int>(), "stackTrace", true,
        Json{{"stackFrames", Json::array({Json{{"id", 1}, {"name", "main"}, {"line", 3}, {"source", Json{{"path", path.string()}}}}})}}));
    REQUIRE(fixture.manager.State() == DapManager::SessionState::Stopped);

    REQUIRE(fixture.manager.RunToCursor(path, 10) == "Running to cursor...");

    // The temporary breakpoint is pushed to the adapter immediately.
    const Json setBreakpoints = fixture.reader.Next();
    REQUIRE(setBreakpoints["command"] == "setBreakpoints");
    REQUIRE(setBreakpoints["arguments"]["breakpoints"] == Json::array({Json{{"line", 10}}}));
    fixture.client->DispatchFrame(ResponseFrame(setBreakpoints["seq"].get<int>(), "setBreakpoints", true,
                                                Json{{"breakpoints", Json::array({Json{{"verified", true}, {"line", 10}}})}}));
    REQUIRE(fixture.manager.BreakpointsForFile(path) == std::vector<std::size_t>{10});

    // Then continue, targeting the currently stopped thread.
    const Json continueRequest = fixture.reader.Next();
    REQUIRE(continueRequest["command"] == "continue");
    REQUIRE(continueRequest["arguments"]["threadId"] == 3);
    fixture.client->DispatchFrame(ResponseFrame(continueRequest["seq"].get<int>(), "continue", true));
    REQUIRE(fixture.manager.State() == DapManager::SessionState::Running);

    // The debuggee stops again (wherever it landed) -- the temporary
    // breakpoint is cleared right away, before the stackTrace request even
    // goes out.
    fixture.client->DispatchFrame(EventFrame("stopped", Json{{"reason", "breakpoint"}, {"threadId", 3}}));
    const Json clearBreakpoints = fixture.reader.Next();
    REQUIRE(clearBreakpoints["command"] == "setBreakpoints");
    REQUIRE(clearBreakpoints["arguments"]["breakpoints"] == Json::array());
    fixture.client->DispatchFrame(
        ResponseFrame(clearBreakpoints["seq"].get<int>(), "setBreakpoints", true, Json{{"breakpoints", Json::array()}}));
    REQUIRE(fixture.manager.BreakpointsForFile(path).empty());

    const Json secondStackTrace = fixture.reader.Next();
    REQUIRE(secondStackTrace["command"] == "stackTrace");
    fixture.client->DispatchFrame(
        ResponseFrame(secondStackTrace["seq"].get<int>(), "stackTrace", true, Json{{"stackFrames", Json::array()}}));

    SetDapLaunchConfig("dap-manager-test-run-to-cursor", "");
}

TEST_CASE("RunToCursor leaves an already-existing breakpoint alone", "[Dap]") {
    ManagerFixture fixture;
    fixture.InjectClient();

    const std::filesystem::path path = std::filesystem::current_path() / "dap-run-to-cursor-existing.c";
    fixture.manager.ToggleBreakpoint(path, 10); // set before the session starts -- pushed on the initialized event

    fixture.StartRunningSession("dap-manager-test-run-to-cursor-existing");
    fixture.client->DispatchFrame(EventFrame("initialized"));
    REQUIRE(fixture.reader.Next()["command"] == "setBreakpoints");
    REQUIRE(fixture.reader.Next()["command"] == "setFunctionBreakpoints");
    REQUIRE(fixture.reader.Next()["command"] == "setExceptionBreakpoints");
    REQUIRE(fixture.reader.Next()["command"] == "configurationDone");

    fixture.client->DispatchFrame(EventFrame("stopped", Json{{"reason", "breakpoint"}, {"threadId", 1}}));
    const Json stackTrace = fixture.reader.Next();
    fixture.client->DispatchFrame(ResponseFrame(stackTrace["seq"].get<int>(), "stackTrace", true, Json{{"stackFrames", Json::array()}}));
    REQUIRE(fixture.manager.State() == DapManager::SessionState::Stopped);

    REQUIRE(fixture.manager.RunToCursor(path, 10) == "Running to cursor...");

    // A real breakpoint already lives at that line -- RunToCursor sends
    // continue directly, no extra setBreakpoints frame first.
    const Json continueRequest = fixture.reader.Next();
    REQUIRE(continueRequest["command"] == "continue");
    fixture.client->DispatchFrame(ResponseFrame(continueRequest["seq"].get<int>(), "continue", true));
    REQUIRE(fixture.manager.State() == DapManager::SessionState::Running);

    // On the next stop, nothing is cleared -- the breakpoint the user
    // actually set survives (asserted by the very next frame being
    // stackTrace, not an unexpected setBreakpoints).
    fixture.client->DispatchFrame(EventFrame("stopped", Json{{"reason", "breakpoint"}, {"threadId", 1}}));
    const Json secondStackTrace = fixture.reader.Next();
    REQUIRE(secondStackTrace["command"] == "stackTrace");
    fixture.client->DispatchFrame(
        ResponseFrame(secondStackTrace["seq"].get<int>(), "stackTrace", true, Json{{"stackFrames", Json::array()}}));
    REQUIRE(fixture.manager.BreakpointsForFile(path) == std::vector<std::size_t>{10});

    SetDapLaunchConfig("dap-manager-test-run-to-cursor-existing", "");
}

TEST_CASE("JumpToLine refuses when the session is not stopped", "[Dap]") {
    ManagerFixture fixture;
    fixture.InjectClient();
    fixture.StartRunningSession("dap-manager-test-jump-refuse");

    bool        finished = false;
    bool        success  = true;
    std::string message;
    fixture.manager.JumpToLine(std::filesystem::current_path() / "dap-jump-refuse.c", 1, [&](bool ok, std::string msg) {
        finished = true;
        success  = ok;
        message  = std::move(msg);
    });
    REQUIRE(finished);
    REQUIRE_FALSE(success);
    REQUIRE(message == "Not stopped (nothing to jump from).");
    SetDapLaunchConfig("dap-manager-test-jump-refuse", "");
}

TEST_CASE("JumpToLine sends gotoTargets then goto, landing via the adapter's returned target id", "[Dap]") {
    ManagerFixture fixture;
    fixture.InjectClient();
    fixture.StartRunningSession("dap-manager-test-jump-to-line");

    const std::filesystem::path path = std::filesystem::current_path() / "dap-jump-to-line.c";

    fixture.client->DispatchFrame(EventFrame("stopped", Json{{"reason", "breakpoint"}, {"threadId", 5}}));
    const Json stackTrace = fixture.reader.Next();
    fixture.client->DispatchFrame(ResponseFrame(stackTrace["seq"].get<int>(), "stackTrace", true, Json{{"stackFrames", Json::array()}}));
    REQUIRE(fixture.manager.State() == DapManager::SessionState::Stopped);

    bool        finished = false;
    bool        success  = false;
    std::string message;
    fixture.manager.JumpToLine(path, 20, [&](bool ok, std::string msg) {
        finished = true;
        success  = ok;
        message  = std::move(msg);
    });

    const Json gotoTargets = fixture.reader.Next();
    REQUIRE(gotoTargets["command"] == "gotoTargets");
    REQUIRE(gotoTargets["arguments"]["source"]["path"] == path.string());
    REQUIRE(gotoTargets["arguments"]["line"] == 20);
    REQUIRE_FALSE(finished); // still waiting on gotoTargets's response

    fixture.client->DispatchFrame(
        ResponseFrame(gotoTargets["seq"].get<int>(), "gotoTargets", true,
                      Json{{"targets", Json::array({Json{{"id", 7}, {"label", "line 20"}, {"line", 20}}})}}));

    const Json gotoRequest = fixture.reader.Next();
    REQUIRE(gotoRequest["command"] == "goto");
    REQUIRE(gotoRequest["arguments"]["threadId"] == 5);
    REQUIRE(gotoRequest["arguments"]["targetId"] == 7);
    REQUIRE_FALSE(finished); // still waiting on goto's own response

    fixture.client->DispatchFrame(ResponseFrame(gotoRequest["seq"].get<int>(), "goto", true));
    REQUIRE(finished);
    REQUIRE(success);
    REQUIRE(message == "Jumped to line.");

    SetDapLaunchConfig("dap-manager-test-jump-to-line", "");
}

TEST_CASE("JumpToLine reports failure when gotoTargets returns no targets", "[Dap]") {
    ManagerFixture fixture;
    fixture.InjectClient();
    fixture.StartRunningSession("dap-manager-test-jump-no-targets");

    fixture.client->DispatchFrame(EventFrame("stopped", Json{{"reason", "breakpoint"}, {"threadId", 1}}));
    const Json stackTrace = fixture.reader.Next();
    fixture.client->DispatchFrame(ResponseFrame(stackTrace["seq"].get<int>(), "stackTrace", true, Json{{"stackFrames", Json::array()}}));

    bool        finished = false;
    bool        success  = true;
    std::string message;
    fixture.manager.JumpToLine(std::filesystem::current_path() / "dap-jump-no-targets.c", 40, [&](bool ok, std::string msg) {
        finished = true;
        success  = ok;
        message  = std::move(msg);
    });
    const Json gotoTargets = fixture.reader.Next();
    fixture.client->DispatchFrame(ResponseFrame(gotoTargets["seq"].get<int>(), "gotoTargets", true, Json{{"targets", Json::array()}}));

    REQUIRE(finished);
    REQUIRE_FALSE(success);
    REQUIRE(message == "No jump target available at that line.");
    SetDapLaunchConfig("dap-manager-test-jump-no-targets", "");
}

TEST_CASE("JumpToLine's failure message notes when the adapter never advertised gotoTargets support", "[Dap]") {
    ManagerFixture fixture;
    fixture.InjectClient();
    fixture.StartRunningSession("dap-manager-test-jump-unsupported"); // no supportsGotoTargetsRequest in the initialize response

    fixture.client->DispatchFrame(EventFrame("stopped", Json{{"reason", "breakpoint"}, {"threadId", 1}}));
    const Json stackTrace = fixture.reader.Next();
    fixture.client->DispatchFrame(ResponseFrame(stackTrace["seq"].get<int>(), "stackTrace", true, Json{{"stackFrames", Json::array()}}));

    bool        finished = false;
    bool        success  = true;
    std::string message;
    fixture.manager.JumpToLine(std::filesystem::current_path() / "dap-jump-unsupported.c", 12, [&](bool ok, std::string msg) {
        finished = true;
        success  = ok;
        message  = std::move(msg);
    });
    const Json gotoTargets = fixture.reader.Next();
    fixture.client->DispatchFrame(
        ResponseFrame(gotoTargets["seq"].get<int>(), "gotoTargets", false, Json::object(), "unsupported request"));

    REQUIRE(finished);
    REQUIRE_FALSE(success);
    REQUIRE(message.find("did not advertise jump-to-line support") != std::string::npos);
    SetDapLaunchConfig("dap-manager-test-jump-unsupported", "");
}

TEST_CASE("StepInto and StepOut send their own DAP request names", "[Dap]") {
    ManagerFixture fixture;
    fixture.InjectClient();
    fixture.StartRunningSession("dap-manager-test-step-kinds");

    fixture.client->DispatchFrame(EventFrame("stopped", Json{{"reason", "step"}, {"threadId", 1}}));
    const Json stackTrace = fixture.reader.Next();
    fixture.client->DispatchFrame(ResponseFrame(stackTrace["seq"].get<int>(), "stackTrace", true,
                                                Json{{"stackFrames", Json::array()}}));

    REQUIRE(fixture.manager.StepInto() == "Stepping into...");
    REQUIRE(fixture.reader.Next()["command"] == "stepIn");

    // The stepIn response never arrived (session still Stopped as far as
    // the manager knows), so stepping again is still legal.
    REQUIRE(fixture.manager.StepOut() == "Stepping out...");
    REQUIRE(fixture.reader.Next()["command"] == "stepOut");
    SetDapLaunchConfig("dap-manager-test-step-kinds", "");
}

TEST_CASE("RequestScopes and RequestVariables parse the adapter's response bodies", "[Dap]") {
    ManagerFixture fixture;
    fixture.InjectClient();
    fixture.StartRunningSession("dap-manager-test-scopes");

    fixture.client->DispatchFrame(EventFrame("stopped", Json{{"reason", "breakpoint"}, {"threadId", 1}}));
    const Json stopTrace = fixture.reader.Next();
    fixture.client->DispatchFrame(ResponseFrame(stopTrace["seq"].get<int>(), "stackTrace", true,
                                                Json{{"stackFrames", Json::array()}}));

    std::vector<DapManager::Scope> scopes;
    fixture.manager.RequestScopes(9, [&](std::vector<DapManager::Scope> result) { scopes = std::move(result); });
    const Json scopesRequest = fixture.reader.Next();
    REQUIRE(scopesRequest["command"] == "scopes");
    REQUIRE(scopesRequest["arguments"]["frameId"] == 9);
    fixture.client->DispatchFrame(ResponseFrame(
        scopesRequest["seq"].get<int>(), "scopes", true,
        Json{{"scopes", Json::array({Json{{"name", "Locals"}, {"variablesReference", 100}},
                                     Json{{"name", "Globals"}, {"variablesReference", 101}}})}}));
    REQUIRE(scopes.size() == 2);
    REQUIRE(scopes[0].name == "Locals");
    REQUIRE(scopes[0].variablesReference == 100);

    std::vector<DapManager::Variable> variables;
    fixture.manager.RequestVariables(100, [&](std::vector<DapManager::Variable> result) { variables = std::move(result); });
    const Json variablesRequest = fixture.reader.Next();
    REQUIRE(variablesRequest["command"] == "variables");
    REQUIRE(variablesRequest["arguments"]["variablesReference"] == 100);
    fixture.client->DispatchFrame(ResponseFrame(
        variablesRequest["seq"].get<int>(), "variables", true,
        Json{{"variables", Json::array({Json{{"name", "x"}, {"value", "1"}, {"type", "int"}, {"variablesReference", 0}},
                                        Json{{"name", "s"}, {"value", "{...}"}, {"variablesReference", 200}}})}}));
    REQUIRE(variables.size() == 2);
    REQUIRE(variables[0].name == "x");
    REQUIRE(variables[0].type == "int");
    REQUIRE(variables[0].variablesReference == 0);
    REQUIRE(variables[1].variablesReference == 200);
    SetDapLaunchConfig("dap-manager-test-scopes", "");
}

TEST_CASE("Evaluate scopes the expression to the stopped top frame and reports the result", "[Dap]") {
    ManagerFixture fixture;
    fixture.InjectClient();
    fixture.StartRunningSession("dap-manager-test-evaluate");

    fixture.client->DispatchFrame(EventFrame("stopped", Json{{"reason", "breakpoint"}, {"threadId", 1}}));
    const Json stackTrace = fixture.reader.Next();
    fixture.client->DispatchFrame(ResponseFrame(
        stackTrace["seq"].get<int>(), "stackTrace", true,
        Json{{"stackFrames",
              Json::array({Json{{"id", 31}, {"name", "main"}, {"line", 3}, {"source", Json{{"path", "/tmp/e.c"}}}}})}}));

    bool        ok = false;
    std::string text;
    fixture.manager.Evaluate("x + 1", [&](bool success, std::string result) {
        ok   = success;
        text = std::move(result);
    });
    const Json evaluate = fixture.reader.Next();
    REQUIRE(evaluate["command"] == "evaluate");
    REQUIRE(evaluate["arguments"]["expression"] == "x + 1");
    REQUIRE(evaluate["arguments"]["frameId"] == 31);
    REQUIRE(evaluate["arguments"]["context"] == "repl");
    fixture.client->DispatchFrame(ResponseFrame(evaluate["seq"].get<int>(), "evaluate", true, Json{{"result", "2"}}));
    REQUIRE(ok);
    REQUIRE(text == "2");
    SetDapLaunchConfig("dap-manager-test-evaluate", "");
}

TEST_CASE("RequestVariables/Evaluate's hex parameter sends DAP's format:{hex:true} hint, omitted by default",
          "[Dap]") {
    ManagerFixture fixture;
    fixture.InjectClient();
    fixture.StartRunningSession("dap-manager-test-hex-format");

    fixture.client->DispatchFrame(EventFrame("stopped", Json{{"reason", "breakpoint"}, {"threadId", 1}}));
    const Json stackTrace = fixture.reader.Next();
    fixture.client->DispatchFrame(ResponseFrame(stackTrace["seq"].get<int>(), "stackTrace", true, Json{{"stackFrames", Json::array()}}));

    // Default (hex=false, every pre-existing call site): no "format" field
    // at all, byte-for-byte the same request every prior test already
    // asserts.
    fixture.manager.RequestVariables(100, [](std::vector<DapManager::Variable>) {});
    const Json plainVariables = fixture.reader.Next();
    REQUIRE_FALSE(plainVariables["arguments"].contains("format"));

    fixture.manager.RequestVariables(100, [](std::vector<DapManager::Variable>) {}, /*hex=*/true);
    const Json hexVariables = fixture.reader.Next();
    REQUIRE(hexVariables["arguments"]["format"] == Json{{"hex", true}});

    fixture.manager.Evaluate("x", [](bool, std::string) {});
    const Json plainEvaluate = fixture.reader.Next();
    REQUIRE_FALSE(plainEvaluate["arguments"].contains("format"));

    fixture.manager.Evaluate("x", [](bool, std::string) {}, "repl", /*hex=*/true);
    const Json hexEvaluate = fixture.reader.Next();
    REQUIRE(hexEvaluate["arguments"]["format"] == Json{{"hex", true}});

    SetDapLaunchConfig("dap-manager-test-hex-format", "");
}

TEST_CASE("Evaluate without a session fails immediately", "[Dap]") {
    ned::ui::EventLoop eventLoop;
    DapManager         manager(eventLoop);

    bool        ok = true;
    std::string text;
    manager.Evaluate("x", [&](bool success, std::string result) {
        ok   = success;
        text = std::move(result);
    });
    REQUIRE_FALSE(ok);
    REQUIRE(text == "No debug session.");
}

TEST_CASE("A terminated event ends the session and reports it", "[Dap]") {
    ManagerFixture fixture;
    fixture.InjectClient();
    fixture.StartRunningSession("dap-manager-test-terminated");

    std::string endedReason;
    fixture.manager.SetOnSessionEnded([&](std::string reason) { endedReason = std::move(reason); });

    // lsp-use-after-free follow-up: EndSession now destroys the DapClient
    // directly (immediate destruction is safe now that DapClient itself
    // guards against a stray Post()ed callback -- see LspClient.h's own
    // header comment on alive_), which joins its background read thread as
    // part of destruction. That thread is deliberately still blocked in a
    // real blocking read on this fixture's fake pipe (nothing ever sent it
    // real EOF) -- closing the fixture's own write end first gives it real
    // EOF, matching a real debug adapter process actually exiting.
    ::close(fixture.adapterStdoutWrite);
    fixture.adapterStdoutWrite = -1; // fixture's own destructor must not double-close

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

    // lsp-use-after-free follow-up: see "A terminated event ends the
    // session and reports it"'s identical comment just above -- StopSession
    // -> EndSession destroying the DapClient directly would otherwise join
    // this fixture's deliberately-still-blocked read thread and hang.
    ::close(fixture.adapterStdoutWrite);
    fixture.adapterStdoutWrite = -1; // fixture's own destructor must not double-close

    REQUIRE(fixture.manager.StopSession() == "Debug session stopped.");
    REQUIRE(fixture.manager.State() == DapManager::SessionState::Inactive);
    REQUIRE(endedReason == "Debug session stopped.");

    const Json disconnect = fixture.reader.Next();
    REQUIRE(disconnect["command"] == "disconnect");
    REQUIRE(disconnect["arguments"]["terminateDebuggee"] == true);
    SetDapLaunchConfig("dap-manager-test-stop", "");
}

// DAP round 2 below.

TEST_CASE("SetBreakpointCondition/LogMessage create-or-update and send the fields, verified updates from the response",
         "[Dap]") {
    ManagerFixture fixture;
    fixture.InjectClient();
    fixture.StartRunningSession("dap-manager-test-condition");

    const std::filesystem::path path = std::filesystem::current_path() / "dap-test-condition.c";
    const std::string status = fixture.manager.SetBreakpointCondition(path, 5, "x > 1");
    REQUIRE(status.starts_with("Condition set at dap-test-condition.c:5"));

    const Json setBreakpoints = fixture.reader.Next();
    REQUIRE(setBreakpoints["command"] == "setBreakpoints");
    REQUIRE(setBreakpoints["arguments"]["breakpoints"] == Json::array({Json{{"line", 5}, {"condition", "x > 1"}}}));

    const auto breakpoints = fixture.manager.BreakpointsForKey(DapManager::NormalizePathKey(path));
    REQUIRE(breakpoints.size() == 1);
    REQUIRE(breakpoints[0].condition == "x > 1");
    REQUIRE(breakpoints[0].verified); // optimistic before any response

    fixture.client->DispatchFrame(ResponseFrame(setBreakpoints["seq"].get<int>(), "setBreakpoints", true,
                                                Json{{"breakpoints", Json::array({Json{{"verified", false}}})}}));
    REQUIRE_FALSE(fixture.manager.BreakpointsForKey(DapManager::NormalizePathKey(path))[0].verified);

    // A log message on the same line, and clearing the condition -- both go
    // through the same find-or-create/send path.
    fixture.manager.SetBreakpointLogMessage(path, 5, "hit: {x}");
    const Json withLog = fixture.reader.Next();
    REQUIRE(withLog["arguments"]["breakpoints"] == Json::array({Json{{"line", 5}, {"condition", "x > 1"}, {"logMessage", "hit: {x}"}}}));

    const std::string cleared = fixture.manager.SetBreakpointCondition(path, 5, "");
    REQUIRE(cleared.starts_with("Condition cleared"));
    const Json afterClear = fixture.reader.Next();
    REQUIRE(afterClear["arguments"]["breakpoints"] == Json::array({Json{{"line", 5}, {"logMessage", "hit: {x}"}}}));
    SetDapLaunchConfig("dap-manager-test-condition", "");
}

TEST_CASE("StartOrContinue parses conditional/logpoint/setVariable capabilities from the initialize response", "[Dap]") {
    ManagerFixture fixture;
    fixture.InjectClient();
    SetDapLaunchConfig("dap-manager-test-capabilities", R"({"program": "./fake"})");
    fixture.manager.StartOrContinue("dap-manager-test-capabilities");
    const Json initialize = fixture.reader.Next();
    fixture.client->DispatchFrame(ResponseFrame(
        initialize["seq"].get<int>(), "initialize", true,
        Json{{"supportsConditionalBreakpoints", true}, {"supportsLogPoints", false}, {"supportsSetVariable", true}}));
    fixture.reader.Next(); // launch

    // Capability is surfaced only as a soft warning appended to the status
    // string, only for the field the adapter said it lacks.
    const std::filesystem::path path = std::filesystem::current_path() / "dap-test-caps.c";
    REQUIRE(fixture.manager.SetBreakpointCondition(path, 1, "true").find("did not advertise") == std::string::npos);
    fixture.reader.Next(); // setBreakpoints for the condition above
    REQUIRE(fixture.manager.SetBreakpointLogMessage(path, 1, "x").find("did not advertise logpoint") != std::string::npos);
    SetDapLaunchConfig("dap-manager-test-capabilities", "");
}

TEST_CASE("Watches are added, evaluated with watch context, and removable by index", "[Dap]") {
    ned::ui::EventLoop eventLoop;
    DapManager         manager(eventLoop);
    REQUIRE(manager.Watches().empty());
    manager.AddWatch("x");
    manager.AddWatch("y");
    REQUIRE(manager.Watches() == std::vector<std::string>{"x", "y"});
    manager.RemoveWatchAt(0);
    REQUIRE(manager.Watches() == std::vector<std::string>{"y"});
    manager.RemoveWatchAt(99); // out of range -- safe no-op
    REQUIRE(manager.Watches() == std::vector<std::string>{"y"});
}

TEST_CASE("Evaluate's context parameter defaults to repl and is overridable for watches", "[Dap]") {
    ManagerFixture fixture;
    fixture.InjectClient();
    fixture.StartRunningSession("dap-manager-test-watch-context");
    fixture.client->DispatchFrame(EventFrame("stopped", Json{{"reason", "breakpoint"}, {"threadId", 1}}));
    fixture.reader.Next(); // stackTrace

    fixture.manager.Evaluate("y", [](bool, std::string) {}, "watch");
    const Json evaluate = fixture.reader.Next();
    REQUIRE(evaluate["arguments"]["context"] == "watch");
    SetDapLaunchConfig("dap-manager-test-watch-context", "");
}

TEST_CASE("Watch history accumulates a numeric value per stop, skipping non-numeric evaluations", "[Dap]") {
    ManagerFixture fixture;
    fixture.InjectClient();
    fixture.StartRunningSession("dap-manager-test-watch-history");

    fixture.manager.AddWatch("counter");
    REQUIRE(fixture.manager.WatchHistoryAt(0).empty());

    auto stop = [&](int threadId) {
        fixture.client->DispatchFrame(EventFrame("stopped", Json{{"reason", "breakpoint"}, {"threadId", threadId}}));
        const Json stackTrace = fixture.reader.Next();
        fixture.client->DispatchFrame(
            ResponseFrame(stackTrace["seq"].get<int>(), "stackTrace", true, Json{{"stackFrames", Json::array()}}));
    };
    auto answerWatchEvaluate = [&](const std::string& value) {
        const Json evaluate = fixture.reader.Next();
        REQUIRE(evaluate["command"] == "evaluate");
        REQUIRE(evaluate["arguments"]["expression"] == "counter");
        REQUIRE(evaluate["arguments"]["context"] == "watch");
        fixture.client->DispatchFrame(ResponseFrame(evaluate["seq"].get<int>(), "evaluate", true, Json{{"result", value}}));
    };
    auto resume = [&]() {
        REQUIRE(fixture.manager.StartOrContinue("dap-manager-test-watch-history") == "Continuing.");
        const Json cont = fixture.reader.Next();
        REQUIRE(cont["command"] == "continue");
        fixture.client->DispatchFrame(ResponseFrame(cont["seq"].get<int>(), "continue", true));
    };

    stop(1);
    answerWatchEvaluate("1");
    REQUIRE(fixture.manager.WatchHistoryAt(0) == std::vector<double>{1.0});

    resume();
    stop(1);
    answerWatchEvaluate("<optimized out>"); // non-numeric -- skipped, not recorded as a gap
    REQUIRE(fixture.manager.WatchHistoryAt(0) == std::vector<double>{1.0});

    resume();
    stop(1);
    answerWatchEvaluate("2.5");
    REQUIRE(fixture.manager.WatchHistoryAt(0) == std::vector<double>{1.0, 2.5});

    REQUIRE(fixture.manager.WatchHistoryAt(99).empty()); // out-of-range index -- safe empty

    SetDapLaunchConfig("dap-manager-test-watch-history", "");
}

TEST_CASE("RemoveWatchAt keeps watch history indices in sync with Watches()", "[Dap]") {
    ManagerFixture fixture;
    fixture.InjectClient();
    fixture.StartRunningSession("dap-manager-test-watch-history-remove");

    fixture.manager.AddWatch("a");
    fixture.manager.AddWatch("b");

    fixture.client->DispatchFrame(EventFrame("stopped", Json{{"reason", "breakpoint"}, {"threadId", 1}}));
    const Json stackTrace = fixture.reader.Next();
    fixture.client->DispatchFrame(
        ResponseFrame(stackTrace["seq"].get<int>(), "stackTrace", true, Json{{"stackFrames", Json::array()}}));

    const Json evalA = fixture.reader.Next();
    REQUIRE(evalA["arguments"]["expression"] == "a");
    fixture.client->DispatchFrame(ResponseFrame(evalA["seq"].get<int>(), "evaluate", true, Json{{"result", "10"}}));
    const Json evalB = fixture.reader.Next();
    REQUIRE(evalB["arguments"]["expression"] == "b");
    fixture.client->DispatchFrame(ResponseFrame(evalB["seq"].get<int>(), "evaluate", true, Json{{"result", "20"}}));

    REQUIRE(fixture.manager.WatchHistoryAt(0) == std::vector<double>{10.0});
    REQUIRE(fixture.manager.WatchHistoryAt(1) == std::vector<double>{20.0});

    fixture.manager.RemoveWatchAt(0);
    REQUIRE(fixture.manager.Watches() == std::vector<std::string>{"b"});
    REQUIRE(fixture.manager.WatchHistoryAt(0) == std::vector<double>{20.0}); // "b"'s own history, shifted down with it

    SetDapLaunchConfig("dap-manager-test-watch-history-remove", "");
}

TEST_CASE("A fresh session starts every watch with an empty history", "[Dap]") {
    ManagerFixture fixture;
    fixture.InjectClient();
    fixture.manager.AddWatch("x");
    fixture.StartRunningSession("dap-manager-test-watch-history-fresh-session");

    fixture.client->DispatchFrame(EventFrame("stopped", Json{{"reason", "breakpoint"}, {"threadId", 1}}));
    const Json stackTrace = fixture.reader.Next();
    fixture.client->DispatchFrame(
        ResponseFrame(stackTrace["seq"].get<int>(), "stackTrace", true, Json{{"stackFrames", Json::array()}}));
    const Json evaluate = fixture.reader.Next();
    fixture.client->DispatchFrame(ResponseFrame(evaluate["seq"].get<int>(), "evaluate", true, Json{{"result", "1"}}));
    REQUIRE(fixture.manager.WatchHistoryAt(0) == std::vector<double>{1.0});

    SetDapLaunchConfig("dap-manager-test-watch-history-fresh-session", "");
}

TEST_CASE("EvaluateWithReference exposes the response's variablesReference for graphable-value detection", "[Dap]") {
    ManagerFixture fixture;
    fixture.InjectClient();
    fixture.StartRunningSession("dap-manager-test-evaluate-with-reference");
    fixture.client->DispatchFrame(EventFrame("stopped", Json{{"reason", "breakpoint"}, {"threadId", 1}}));
    fixture.reader.Next(); // stackTrace, left unanswered -- frameId isn't under test here

    DapManager::EvaluateResult result;
    fixture.manager.EvaluateWithReference("arr", [&](DapManager::EvaluateResult r) { result = r; });
    const Json evaluate = fixture.reader.Next();
    REQUIRE(evaluate["arguments"]["expression"] == "arr");
    REQUIRE(evaluate["arguments"]["context"] == "watch");
    fixture.client->DispatchFrame(
        ResponseFrame(evaluate["seq"].get<int>(), "evaluate", true, Json{{"result", "{...}"}, {"variablesReference", 77}}));

    REQUIRE(result.success);
    REQUIRE(result.text == "{...}");
    REQUIRE(result.variablesReference == 77);

    SetDapLaunchConfig("dap-manager-test-evaluate-with-reference", "");
}

TEST_CASE("EvaluateWithReference fails gracefully without a session", "[Dap]") {
    ned::ui::EventLoop eventLoop;
    DapManager         manager(eventLoop);

    DapManager::EvaluateResult result;
    result.success            = true;
    result.variablesReference = 1;
    manager.EvaluateWithReference("x", [&](DapManager::EvaluateResult r) { result = r; });
    REQUIRE_FALSE(result.success);
    REQUIRE(result.variablesReference == 0);
}

TEST_CASE("RequestThreads parses the adapter's thread list", "[Dap]") {
    ManagerFixture fixture;
    fixture.InjectClient();
    fixture.StartRunningSession("dap-manager-test-threads");
    fixture.client->DispatchFrame(EventFrame("stopped", Json{{"reason", "breakpoint"}, {"threadId", 1}}));
    fixture.reader.Next(); // stackTrace

    std::vector<DapManager::Thread> threads;
    fixture.manager.RequestThreads([&](std::vector<DapManager::Thread> result) { threads = std::move(result); });
    const Json request = fixture.reader.Next();
    REQUIRE(request["command"] == "threads");
    fixture.client->DispatchFrame(ResponseFrame(
        request["seq"].get<int>(), "threads", true,
        Json{{"threads", Json::array({Json{{"id", 1}, {"name", "main"}}, Json{{"id", 2}, {"name", "worker"}}})}}));
    REQUIRE(threads.size() == 2);
    REQUIRE(threads[1].id == 2);
    REQUIRE(threads[1].name == "worker");
    SetDapLaunchConfig("dap-manager-test-threads", "");
}

TEST_CASE("SelectThread refocuses inspection/stepping/continue at the chosen thread", "[Dap]") {
    ManagerFixture fixture;
    fixture.InjectClient();
    fixture.StartRunningSession("dap-manager-test-select-thread");
    fixture.client->DispatchFrame(EventFrame("stopped", Json{{"reason", "breakpoint"}, {"threadId", 1}}));
    const Json initialStackTrace = fixture.reader.Next();
    fixture.client->DispatchFrame(ResponseFrame(initialStackTrace["seq"].get<int>(), "stackTrace", true,
                                                Json{{"stackFrames", Json::array()}}));

    bool selected = false;
    fixture.manager.SelectThread(2, [&](bool success) { selected = success; });
    const Json refreshStackTrace = fixture.reader.Next();
    REQUIRE(refreshStackTrace["command"] == "stackTrace");
    REQUIRE(refreshStackTrace["arguments"]["threadId"] == 2);
    fixture.client->DispatchFrame(ResponseFrame(
        refreshStackTrace["seq"].get<int>(), "stackTrace", true,
        Json{{"stackFrames", Json::array({Json{{"id", 77}, {"name", "worker"}, {"line", 1}, {"source", Json{{"path", "/tmp/w.c"}}}}})}}));
    REQUIRE(selected);

    // Subsequent requests target the newly focused thread, not the one that
    // originally reported `stopped`.
    fixture.manager.RequestStackTrace([](std::vector<DapManager::StackFrame>) {});
    REQUIRE(fixture.reader.Next()["arguments"]["threadId"] == 2);

    REQUIRE(fixture.manager.StepOver() == "Stepping over...");
    REQUIRE(fixture.reader.Next()["arguments"]["threadId"] == 2);
    SetDapLaunchConfig("dap-manager-test-select-thread", "");
}

TEST_CASE("SetVariable sends variablesReference/name/value and parses the result", "[Dap]") {
    ManagerFixture fixture;
    fixture.InjectClient();
    fixture.StartRunningSession("dap-manager-test-set-variable");
    fixture.client->DispatchFrame(EventFrame("stopped", Json{{"reason", "breakpoint"}, {"threadId", 1}}));
    fixture.reader.Next(); // stackTrace

    DapManager::SetVariableResult result;
    fixture.manager.SetVariable(100, "x", "42", [&](DapManager::SetVariableResult r) { result = std::move(r); });
    const Json request = fixture.reader.Next();
    REQUIRE(request["command"] == "setVariable");
    REQUIRE(request["arguments"]["variablesReference"] == 100);
    REQUIRE(request["arguments"]["name"] == "x");
    REQUIRE(request["arguments"]["value"] == "42");
    fixture.client->DispatchFrame(
        ResponseFrame(request["seq"].get<int>(), "setVariable", true, Json{{"value", "42"}, {"type", "int"}, {"variablesReference", 0}}));
    REQUIRE(result.success);
    REQUIRE(result.value == "42");
    REQUIRE(result.type == "int");

    DapManager::SetVariableResult failure;
    fixture.manager.SetVariable(100, "bogus", "1", [&](DapManager::SetVariableResult r) { failure = std::move(r); });
    const Json failingRequest = fixture.reader.Next();
    fixture.client->DispatchFrame(
        ResponseFrame(failingRequest["seq"].get<int>(), "setVariable", false, Json::object(), "no such variable"));
    REQUIRE_FALSE(failure.success);
    REQUIRE(failure.errorMessage == "no such variable");
    SetDapLaunchConfig("dap-manager-test-set-variable", "");
}

// DAP round 3 below.

TEST_CASE("SetBreakpointHitCondition create-or-updates and sends hitCondition, capability-gated warning", "[Dap]") {
    ManagerFixture fixture;
    fixture.InjectClient();
    fixture.StartRunningSession("dap-manager-test-hit-condition");

    const std::filesystem::path path   = std::filesystem::current_path() / "dap-test-hit-condition.c";
    const std::string           status = fixture.manager.SetBreakpointHitCondition(path, 9, "> 5");
    REQUIRE(status.starts_with("Hit condition set at dap-test-hit-condition.c:9"));
    // No capability parsed yet in this fixture -- warns, same as
    // SetBreakpointCondition's own default-false behavior.
    REQUIRE(status.find("did not advertise hit-conditional-breakpoint") != std::string::npos);

    const Json setBreakpoints = fixture.reader.Next();
    REQUIRE(setBreakpoints["command"] == "setBreakpoints");
    REQUIRE(setBreakpoints["arguments"]["breakpoints"] == Json::array({Json{{"line", 9}, {"hitCondition", "> 5"}}}));

    const auto breakpoints = fixture.manager.BreakpointsForKey(DapManager::NormalizePathKey(path));
    REQUIRE(breakpoints.size() == 1);
    REQUIRE(breakpoints[0].hitCondition == "> 5");

    const std::string cleared = fixture.manager.SetBreakpointHitCondition(path, 9, "");
    REQUIRE(cleared.starts_with("Hit condition cleared"));
    const Json afterClear = fixture.reader.Next();
    REQUIRE(afterClear["arguments"]["breakpoints"] == Json::array({Json{{"line", 9}}}));
    SetDapLaunchConfig("dap-manager-test-hit-condition", "");
}

TEST_CASE("ToggleFunctionBreakpoint adds/removes and pushes setFunctionBreakpoints immediately", "[Dap]") {
    ManagerFixture fixture;
    fixture.InjectClient();
    fixture.StartRunningSession("dap-manager-test-function-breakpoint");

    REQUIRE(fixture.manager.FunctionBreakpoints().empty());
    REQUIRE(fixture.manager.ToggleFunctionBreakpoint("main"));
    REQUIRE(fixture.manager.FunctionBreakpoints() == std::vector<std::string>{"main"});

    const Json added = fixture.reader.Next();
    REQUIRE(added["command"] == "setFunctionBreakpoints");
    REQUIRE(added["arguments"]["breakpoints"] == Json::array({Json{{"name", "main"}}}));

    REQUIRE_FALSE(fixture.manager.ToggleFunctionBreakpoint("main")); // now removed
    REQUIRE(fixture.manager.FunctionBreakpoints().empty());
    const Json removed = fixture.reader.Next();
    REQUIRE(removed["command"] == "setFunctionBreakpoints");
    REQUIRE(removed["arguments"]["breakpoints"] == Json::array());
    SetDapLaunchConfig("dap-manager-test-function-breakpoint", "");
}

TEST_CASE("Exception breakpoint filters seed from initialize defaults and SetExceptionBreakpointFilters pushes live",
          "[Dap]") {
    ManagerFixture fixture;
    fixture.InjectClient();
    SetDapLaunchConfig("dap-manager-test-exception-filters", R"({"program": "./fake"})");
    fixture.manager.StartOrContinue("dap-manager-test-exception-filters");
    const Json initialize = fixture.reader.Next();
    fixture.client->DispatchFrame(ResponseFrame(
        initialize["seq"].get<int>(), "initialize", true,
        Json{{"exceptionBreakpointFilters", Json::array({Json{{"filter", "raised"}, {"label", "Raised"}, {"default", true}},
                                                         Json{{"filter", "uncaught"}, {"label", "Uncaught"}, {"default", false}}})}}));
    const Json launch = fixture.reader.Next();
    REQUIRE(launch["command"] == "launch");
    fixture.client->DispatchFrame(ResponseFrame(launch["seq"].get<int>(), "launch", true));

    REQUIRE(fixture.manager.AvailableExceptionFilters().size() == 2);
    REQUIRE(fixture.manager.EnabledExceptionFilters() == std::set<std::string>{"raised"}); // only the default-true one

    fixture.manager.SetExceptionBreakpointFilters({"raised", "uncaught"});
    const Json setExceptionBreakpoints = fixture.reader.Next();
    REQUIRE(setExceptionBreakpoints["command"] == "setExceptionBreakpoints");
    const std::set<std::string> sentIds(setExceptionBreakpoints["arguments"]["filters"].begin(),
                                        setExceptionBreakpoints["arguments"]["filters"].end());
    REQUIRE(sentIds == std::set<std::string>{"raised", "uncaught"});
    SetDapLaunchConfig("dap-manager-test-exception-filters", "");
}

TEST_CASE("StartOrContinue parses hit-conditional/function-breakpoint capabilities from the initialize response", "[Dap]") {
    ManagerFixture fixture;
    fixture.InjectClient();
    SetDapLaunchConfig("dap-manager-test-caps-round3", R"({"program": "./fake"})");
    fixture.manager.StartOrContinue("dap-manager-test-caps-round3");
    const Json initialize = fixture.reader.Next();
    fixture.client->DispatchFrame(ResponseFrame(initialize["seq"].get<int>(), "initialize", true,
                                                Json{{"supportsHitConditionalBreakpoints", true}, {"supportsFunctionBreakpoints", false}}));
    fixture.reader.Next(); // launch

    const std::filesystem::path path = std::filesystem::current_path() / "dap-test-caps-round3.c";
    REQUIRE(fixture.manager.SetBreakpointHitCondition(path, 1, "> 1").find("did not advertise") == std::string::npos);
    SetDapLaunchConfig("dap-manager-test-caps-round3", "");
}

TEST_CASE("Attach refuses without an attach configuration", "[Dap]") {
    ned::ui::EventLoop eventLoop;
    DapManager         manager(eventLoop);

    const std::string status = manager.Attach("dap-manager-test-unconfigured-attach");
    REQUIRE(status == "No attach configuration for dap-manager-test-unconfigured-attach (ned/set-dap-attach).");
}

TEST_CASE("Attach sends an attach request (not launch) and StopSession never terminates the debuggee", "[Dap]") {
    ManagerFixture fixture;
    fixture.InjectClient();
    fixture.StartAttachedSession("dap-manager-test-attach");

    std::string endedReason;
    fixture.manager.SetOnSessionEnded([&](std::string reason) { endedReason = std::move(reason); });
    ::close(fixture.adapterStdoutWrite); // see "StopSession sends a disconnect..." for why
    fixture.adapterStdoutWrite = -1;

    REQUIRE(fixture.manager.StopSession() == "Debug session stopped.");
    REQUIRE(endedReason == "Debug session stopped.");
    const Json disconnect = fixture.reader.Next();
    REQUIRE(disconnect["command"] == "disconnect");
    REQUIRE(disconnect["arguments"]["terminateDebuggee"] == false); // attach never kills a process ned didn't start
    SetDapAttachConfig("dap-manager-test-attach", "");
}

TEST_CASE("A launched session's StopSession still terminates the debuggee", "[Dap]") {
    ManagerFixture fixture;
    fixture.InjectClient();
    fixture.StartRunningSession("dap-manager-test-launch-terminate");

    ::close(fixture.adapterStdoutWrite);
    fixture.adapterStdoutWrite = -1;

    fixture.manager.StopSession();
    const Json disconnect = fixture.reader.Next();
    REQUIRE(disconnect["arguments"]["terminateDebuggee"] == true);
    SetDapLaunchConfig("dap-manager-test-launch-terminate", "");
}

// DAP round 4 below.

TEST_CASE("RestartFrame sends restartFrame with the given frameId and resumes on success", "[Dap]") {
    ManagerFixture fixture;
    fixture.InjectClient();
    fixture.StartRunningSession("dap-manager-test-restart-frame");

    fixture.client->DispatchFrame(EventFrame("stopped", Json{{"reason", "breakpoint"}, {"threadId", 1}}));
    const Json stackTrace = fixture.reader.Next();
    fixture.client->DispatchFrame(ResponseFrame(stackTrace["seq"].get<int>(), "stackTrace", true, Json{{"stackFrames", Json::array()}}));

    REQUIRE(fixture.manager.RestartFrame(7) == "Restarting frame... (adapter did not advertise restart-frame support -- may be ignored)");
    const Json restart = fixture.reader.Next();
    REQUIRE(restart["command"] == "restartFrame");
    REQUIRE(restart["arguments"]["frameId"] == 7);
    fixture.client->DispatchFrame(ResponseFrame(restart["seq"].get<int>(), "restartFrame", true));
    REQUIRE(fixture.manager.State() == DapManager::SessionState::Running);

    // Refused outright while running -- same shape as StepInto's own guard.
    REQUIRE(fixture.manager.RestartFrame(7) == "Not stopped (nothing to restart).");
    SetDapLaunchConfig("dap-manager-test-restart-frame", "");
}

TEST_CASE("StartOrContinue parses restartFrame capability, dropping the warning suffix", "[Dap]") {
    ManagerFixture fixture;
    fixture.InjectClient();
    SetDapLaunchConfig("dap-manager-test-restart-caps", R"({"program": "./fake"})");
    fixture.manager.StartOrContinue("dap-manager-test-restart-caps");
    const Json initialize = fixture.reader.Next();
    fixture.client->DispatchFrame(
        ResponseFrame(initialize["seq"].get<int>(), "initialize", true, Json{{"supportsRestartFrame", true}}));
    fixture.reader.Next(); // launch

    fixture.client->DispatchFrame(EventFrame("stopped", Json{{"reason", "breakpoint"}, {"threadId", 1}}));
    const Json stackTrace = fixture.reader.Next();
    fixture.client->DispatchFrame(ResponseFrame(stackTrace["seq"].get<int>(), "stackTrace", true, Json{{"stackFrames", Json::array()}}));

    REQUIRE(fixture.manager.RestartFrame(1) == "Restarting frame...");
    fixture.reader.Next(); // restartFrame
    SetDapLaunchConfig("dap-manager-test-restart-caps", "");
}

TEST_CASE("SendBreakpointsForFile's response records the adapter's snapped actualLine", "[Dap]") {
    ManagerFixture fixture;
    fixture.InjectClient();
    fixture.StartRunningSession("dap-manager-test-actual-line");

    const std::filesystem::path path = std::filesystem::current_path() / "dap-test-actual-line.c";
    fixture.manager.ToggleBreakpoint(path, 3); // toggled on a comment/blank line, say

    const Json setBreakpoints = fixture.reader.Next();
    REQUIRE(setBreakpoints["command"] == "setBreakpoints");
    const auto beforeResponse = fixture.manager.BreakpointsForKey(DapManager::NormalizePathKey(path));
    REQUIRE(beforeResponse[0].actualLine == 0); // not yet known

    // The adapter snaps it to line 5, the next real statement.
    fixture.client->DispatchFrame(ResponseFrame(setBreakpoints["seq"].get<int>(), "setBreakpoints", true,
                                                Json{{"breakpoints", Json::array({Json{{"verified", true}, {"line", 5}}})}}));
    const auto afterResponse = fixture.manager.BreakpointsForKey(DapManager::NormalizePathKey(path));
    REQUIRE(afterResponse[0].line == 3);       // the requested line -- edits still address this
    REQUIRE(afterResponse[0].actualLine == 5); // where it actually landed
    SetDapLaunchConfig("dap-manager-test-actual-line", "");
}

TEST_CASE("StackTrace/Variables parse instructionPointerReference/memoryReference when the adapter sends them",
          "[Dap]") {
    ManagerFixture fixture;
    fixture.InjectClient();
    fixture.StartRunningSession("dap-manager-test-round5-references");

    fixture.client->DispatchFrame(EventFrame("stopped", Json{{"reason", "breakpoint"}, {"threadId", 1}}));
    const Json autoStackTrace = fixture.reader.Next();
    fixture.client->DispatchFrame(ResponseFrame(
        autoStackTrace["seq"].get<int>(), "stackTrace", true,
        Json{{"stackFrames", Json::array({Json{{"id", 1}, {"name", "main"}, {"instructionPointerReference", "0x1000"}},
                                          Json{{"id", 2}, {"name", "caller"}}})}}));

    std::vector<DapManager::StackFrame> frames;
    fixture.manager.RequestStackTrace([&](std::vector<DapManager::StackFrame> result) { frames = std::move(result); });
    const Json stackTrace = fixture.reader.Next();
    fixture.client->DispatchFrame(ResponseFrame(
        stackTrace["seq"].get<int>(), "stackTrace", true,
        Json{{"stackFrames", Json::array({Json{{"id", 1}, {"name", "main"}, {"instructionPointerReference", "0x1000"}},
                                          Json{{"id", 2}, {"name", "caller"}}})}}));
    REQUIRE(frames.size() == 2);
    REQUIRE(frames[0].instructionPointerReference == "0x1000");
    REQUIRE(frames[1].instructionPointerReference.empty()); // adapter sent none for this frame

    std::vector<DapManager::Variable> variables;
    fixture.manager.RequestVariables(100, [&](std::vector<DapManager::Variable> result) { variables = std::move(result); });
    const Json variablesRequest = fixture.reader.Next();
    fixture.client->DispatchFrame(ResponseFrame(
        variablesRequest["seq"].get<int>(), "variables", true,
        Json{{"variables", Json::array({Json{{"name", "p"}, {"value", "0x2000"}, {"memoryReference", "0x2000"}},
                                        Json{{"name", "x"}, {"value", "1"}}})}}));
    REQUIRE(variables.size() == 2);
    REQUIRE(variables[0].memoryReference == "0x2000");
    REQUIRE(variables[1].memoryReference.empty()); // adapter sent none for this variable
    SetDapLaunchConfig("dap-manager-test-round5-references", "");
}

TEST_CASE("StartOrContinue parses disassemble/readMemory capabilities from the initialize response", "[Dap]") {
    ManagerFixture fixture;
    fixture.InjectClient();
    SetDapLaunchConfig("dap-manager-test-round5-capabilities", R"({"program": "./fake"})");
    fixture.manager.StartOrContinue("dap-manager-test-round5-capabilities");
    const Json initialize = fixture.reader.Next();
    fixture.client->DispatchFrame(ResponseFrame(initialize["seq"].get<int>(), "initialize", true,
                                                Json{{"supportsDisassembleRequest", true}, {"supportsReadMemoryRequest", false}}));
    fixture.reader.Next(); // launch
    // No public accessor exists for these (same as setVariable/
    // functionBreakpoints capabilities) -- this test only pins that parsing
    // the two new fields doesn't throw/crash on a well-formed response.
    SetDapLaunchConfig("dap-manager-test-round5-capabilities", "");
}

TEST_CASE("RequestDisassembly sends disassemble with the given window and parses instructions", "[Dap]") {
    ManagerFixture fixture;
    fixture.InjectClient();
    fixture.StartRunningSession("dap-manager-test-disassemble");
    fixture.client->DispatchFrame(EventFrame("stopped", Json{{"reason", "breakpoint"}, {"threadId", 1}}));
    const Json autoStackTrace = fixture.reader.Next();
    fixture.client->DispatchFrame(
        ResponseFrame(autoStackTrace["seq"].get<int>(), "stackTrace", true, Json{{"stackFrames", Json::array()}}));

    std::vector<DapManager::DisassembledInstruction> instructions;
    fixture.manager.RequestDisassembly("0x1000", -2, 4,
                                       [&](std::vector<DapManager::DisassembledInstruction> result) { instructions = std::move(result); });
    const Json disassemble = fixture.reader.Next();
    REQUIRE(disassemble["command"] == "disassemble");
    REQUIRE(disassemble["arguments"]["memoryReference"] == "0x1000");
    REQUIRE(disassemble["arguments"]["instructionOffset"] == -2);
    REQUIRE(disassemble["arguments"]["instructionCount"] == 4);

    fixture.client->DispatchFrame(ResponseFrame(
        disassemble["seq"].get<int>(), "disassemble", true,
        Json{{"instructions", Json::array({Json{{"address", "0xffe"}, {"instructionBytes", "90"}, {"instruction", "nop"}},
                                           Json{{"address", "0x1000"},
                                                {"instruction", "call foo"},
                                                {"location", Json{{"path", "/tmp/dap-test.c"}}},
                                                {"line", 12}}})}}));
    REQUIRE(instructions.size() == 2);
    REQUIRE(instructions[0].address == "0xffe");
    REQUIRE(instructions[0].instructionBytes == "90");
    REQUIRE_FALSE(instructions[0].path.has_value());
    REQUIRE(instructions[1].instruction == "call foo");
    REQUIRE(instructions[1].path.has_value());
    REQUIRE(*instructions[1].path == std::filesystem::path("/tmp/dap-test.c"));
    REQUIRE(instructions[1].line == 12);
    SetDapLaunchConfig("dap-manager-test-disassemble", "");
}

TEST_CASE("RequestDisassembly is a graceful no-op without a stopped session", "[Dap]") {
    ned::ui::EventLoop eventLoop;
    DapManager         manager(eventLoop);
    bool               called = false;
    manager.RequestDisassembly("0x1000", 0, 4, [&](std::vector<DapManager::DisassembledInstruction> result) {
        called = true;
        REQUIRE(result.empty());
    });
    REQUIRE(called);
}

TEST_CASE("RequestMemory sends readMemory and base64-decodes the response data", "[Dap]") {
    ManagerFixture fixture;
    fixture.InjectClient();
    fixture.StartRunningSession("dap-manager-test-read-memory");
    fixture.client->DispatchFrame(EventFrame("stopped", Json{{"reason", "breakpoint"}, {"threadId", 1}}));
    const Json autoStackTrace = fixture.reader.Next();
    fixture.client->DispatchFrame(
        ResponseFrame(autoStackTrace["seq"].get<int>(), "stackTrace", true, Json{{"stackFrames", Json::array()}}));

    bool                    called = false;
    DapManager::MemoryBlock block;
    fixture.manager.RequestMemory("0x2000", 0, 4, [&](bool success, DapManager::MemoryBlock result) {
        called = true;
        REQUIRE(success);
        block = std::move(result);
    });
    const Json readMemory = fixture.reader.Next();
    REQUIRE(readMemory["command"] == "readMemory");
    REQUIRE(readMemory["arguments"]["memoryReference"] == "0x2000");
    REQUIRE(readMemory["arguments"]["offset"] == 0);
    REQUIRE(readMemory["arguments"]["count"] == 4);

    // "AQIDBA==" is the base64 encoding of bytes {1, 2, 3, 4}.
    fixture.client->DispatchFrame(
        ResponseFrame(readMemory["seq"].get<int>(), "readMemory", true, Json{{"address", "0x2000"}, {"data", "AQIDBA=="}}));
    REQUIRE(called);
    REQUIRE(block.address == "0x2000");
    REQUIRE(block.data == std::vector<std::uint8_t>{1, 2, 3, 4});
    REQUIRE(block.unreadableBytes == 0);
    SetDapLaunchConfig("dap-manager-test-read-memory", "");
}

TEST_CASE("RequestMemory reports a fully-unreadable range as success with empty data", "[Dap]") {
    ManagerFixture fixture;
    fixture.InjectClient();
    fixture.StartRunningSession("dap-manager-test-read-memory-unreadable");
    fixture.client->DispatchFrame(EventFrame("stopped", Json{{"reason", "breakpoint"}, {"threadId", 1}}));
    const Json autoStackTrace = fixture.reader.Next();
    fixture.client->DispatchFrame(
        ResponseFrame(autoStackTrace["seq"].get<int>(), "stackTrace", true, Json{{"stackFrames", Json::array()}}));

    bool                    called = false;
    DapManager::MemoryBlock block;
    fixture.manager.RequestMemory("0xdead", 0, 8, [&](bool success, DapManager::MemoryBlock result) {
        called = true;
        REQUIRE(success);
        block = std::move(result);
    });
    const Json readMemory = fixture.reader.Next();
    fixture.client->DispatchFrame(ResponseFrame(readMemory["seq"].get<int>(), "readMemory", true,
                                                Json{{"address", "0xdead"}, {"unreadableBytes", 8}}));
    REQUIRE(called);
    REQUIRE(block.data.empty());
    REQUIRE(block.unreadableBytes == 8);
    SetDapLaunchConfig("dap-manager-test-read-memory-unreadable", "");
}

TEST_CASE("RequestMemory is a graceful failure without a stopped session", "[Dap]") {
    ned::ui::EventLoop eventLoop;
    DapManager         manager(eventLoop);
    bool               called = false;
    manager.RequestMemory("0x1000", 0, 4, [&](bool success, DapManager::MemoryBlock result) {
        called = true;
        REQUIRE_FALSE(success);
        REQUIRE(result.data.empty());
    });
    REQUIRE(called);
}

// Debugging wishlist: reverse debugging below.

TEST_CASE("ReverseContinue and StepBack refuse when the session is not stopped", "[Dap]") {
    ManagerFixture fixture;
    fixture.InjectClient();
    fixture.StartRunningSession("dap-manager-test-reverse-not-stopped");

    REQUIRE(fixture.manager.ReverseContinue() == "Not stopped (nothing to step).");
    REQUIRE(fixture.manager.StepBack() == "Not stopped (nothing to step).");
}

TEST_CASE("ReverseContinue sends reverseContinue and resumes on success, warning when unadvertised", "[Dap]") {
    ManagerFixture fixture;
    fixture.InjectClient();
    fixture.StartRunningSession("dap-manager-test-reverse-continue"); // no supportsStepBack in the initialize response

    fixture.client->DispatchFrame(EventFrame("stopped", Json{{"reason", "breakpoint"}, {"threadId", 1}}));
    const Json stackTrace = fixture.reader.Next();
    fixture.client->DispatchFrame(ResponseFrame(stackTrace["seq"].get<int>(), "stackTrace", true, Json{{"stackFrames", Json::array()}}));

    REQUIRE(fixture.manager.ReverseContinue() ==
            "Reverse-continuing... (adapter did not advertise reverse-debugging support -- may be ignored)");
    const Json reverseContinue = fixture.reader.Next();
    REQUIRE(reverseContinue["command"] == "reverseContinue");
    REQUIRE(reverseContinue["arguments"]["threadId"] == 1);
    fixture.client->DispatchFrame(ResponseFrame(reverseContinue["seq"].get<int>(), "reverseContinue", true));
    REQUIRE(fixture.manager.State() == DapManager::SessionState::Running);
}

TEST_CASE("StepBack sends stepBack and resumes on success", "[Dap]") {
    ManagerFixture fixture;
    fixture.InjectClient();
    fixture.StartRunningSession("dap-manager-test-step-back");

    fixture.client->DispatchFrame(EventFrame("stopped", Json{{"reason", "breakpoint"}, {"threadId", 1}}));
    const Json stackTrace = fixture.reader.Next();
    fixture.client->DispatchFrame(ResponseFrame(stackTrace["seq"].get<int>(), "stackTrace", true, Json{{"stackFrames", Json::array()}}));

    REQUIRE(fixture.manager.StepBack() == "Stepping back... (adapter did not advertise reverse-debugging support -- may be ignored)");
    const Json stepBack = fixture.reader.Next();
    REQUIRE(stepBack["command"] == "stepBack");
    REQUIRE(stepBack["arguments"]["threadId"] == 1);
    fixture.client->DispatchFrame(ResponseFrame(stepBack["seq"].get<int>(), "stepBack", true));
    REQUIRE(fixture.manager.State() == DapManager::SessionState::Running);
}

TEST_CASE("StartOrContinue parses supportsStepBack, dropping the warning suffix for both requests", "[Dap]") {
    ManagerFixture fixture;
    fixture.InjectClient();
    SetDapLaunchConfig("dap-manager-test-stepback-caps", R"({"program": "./fake"})");
    fixture.manager.StartOrContinue("dap-manager-test-stepback-caps");
    const Json initialize = fixture.reader.Next();
    fixture.client->DispatchFrame(ResponseFrame(initialize["seq"].get<int>(), "initialize", true, Json{{"supportsStepBack", true}}));
    fixture.reader.Next(); // launch

    fixture.client->DispatchFrame(EventFrame("stopped", Json{{"reason", "breakpoint"}, {"threadId", 1}}));
    const Json stackTrace = fixture.reader.Next();
    fixture.client->DispatchFrame(ResponseFrame(stackTrace["seq"].get<int>(), "stackTrace", true, Json{{"stackFrames", Json::array()}}));

    REQUIRE(fixture.manager.StepBack() == "Stepping back...");
    fixture.reader.Next(); // stepBack
    SetDapLaunchConfig("dap-manager-test-stepback-caps", "");
}
