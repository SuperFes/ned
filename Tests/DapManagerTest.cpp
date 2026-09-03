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
