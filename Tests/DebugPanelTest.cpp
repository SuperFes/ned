//
// DebugPanel (Source/UI/DebugPanel.h) -- headless coverage over a real
// Manager, DapThreadsPanelTest.cpp's own fixture pattern. No adapter is
// injected in most cases on purpose: the breakpoint sections are meant to
// work with no session at all, which is exactly what makes the panel the
// place to arm breakpoints before launching.
//

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <utility>

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include "Editor/Dap/Client.h"
#include "Editor/Dap/Config.h"
#include "Editor/Dap/Manager.h"
#include "Editor/Lsp/Transport.h"
#include "TestEvents.h"
#include "UI/DebugPanel.h"
#include "UI/EventLoop.h"
#include "UI/Widget.h"

namespace {

using ned::editor::dap::Client;
using ned::editor::dap::Json;
using ned::editor::dap::Manager;
using ned::editor::dap::SetLaunchConfig;
using ned::editor::lsp::Transport;
using ned::ui::Box;
using ned::ui::Canvas;
using ned::ui::DebugPanel;
using ned::ui::Screen;
using ned::ui::Theme;

constexpr int kWidth  = 48;
constexpr int kHeight = 28; // tall enough that nothing these tests assert on scrolls off

// DapManagerTest.cpp's/DapThreadsPanelTest.cpp's own frame reader,
// duplicated rather than shared -- this codebase's per-test-file fixture
// convention.
struct FrameReader {
    int         fd = -1;
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

    // Next()'s non-failing sibling: std::nullopt when the adapter side has
    // nothing pending. The panel fans its requests out (watches, threads,
    // frames, scopes, variables) and interleaves them with Manager's own
    // stop handling, so a test that asserted an order would be asserting
    // an implementation detail -- ServeAll below answers by command
    // instead, which is what a real adapter does.
    std::optional<Json> TryNext(int waitMs) {
        for (int i = 0; i < 64; ++i) {
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
            // The client writes from its own thread, so "nothing readable
            // right now" is not the same as "nothing coming" -- wait
            // briefly before concluding the fan-out is done.
            pollfd waiting{.fd = fd, .events = POLLIN, .revents = 0};
            if (::poll(&waiting, 1, waitMs) <= 0) {
                break;
            }
            char          chunk[4096];
            const ssize_t n = ::read(fd, chunk, sizeof(chunk));
            if (n <= 0) {
                break;
            }
            buffer.append(chunk, static_cast<std::size_t>(n));
        }
        return std::nullopt;
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
    Manager            manager{eventLoop};
    Theme              theme = ned::ui::DarkTheme();
    DebugPanel         panel{theme, manager};
    Screen             screen{kWidth, kHeight};

    Fixture() {
        panel.Tree().SetBox_(Box{.x_min = 0, .x_max = kWidth - 1, .y_min = 0, .y_max = kHeight - 1});
        // The real wiring (main.cpp): every store mutation refreshes the
        // listing, whoever made it.
        manager.SetOnBreakpointsChanged([this] { panel.Refresh(); });
        manager.SetOnSessionStateChanged([this](Manager::SessionState) { panel.NotifySessionStateChanged(); });
        // main.cpp routes this into the focused pane's minibuffer; here it
        // just records the request so a test can answer it directly.
        panel.SetOnTextEntryRequest([this](std::string label, std::string initialText, std::function<void(std::string)> accept) {
            promptLabel   = std::move(label);
            promptInitial = std::move(initialText);
            promptAccept  = std::move(accept);
        });
        panel.Tree().TakeFocus();
    }

    std::string                      promptLabel;
    std::string                      promptInitial;
    std::function<void(std::string)> promptAccept;

    int         adapterStdinRead   = -1;
    int         adapterStdoutWrite = -1;
    Client*     client             = nullptr;
    FrameReader reader;

    void InjectClient() {
        int clientWritesHere[2];
        int clientReadsHere[2];
        REQUIRE(::pipe(clientWritesHere) == 0);
        REQUIRE(::pipe(clientReadsHere) == 0);
        adapterStdinRead   = clientWritesHere[0];
        adapterStdoutWrite = clientReadsHere[1];
        reader.fd          = adapterStdinRead;
        // TryNext has to be able to say "nothing pending" rather than block
        // forever once the fan-out is exhausted.
        REQUIRE(::fcntl(adapterStdinRead, F_SETFL, ::fcntl(adapterStdinRead, F_GETFL, 0) | O_NONBLOCK) == 0);
        client = &manager.SetClientForTesting(
            std::make_unique<Client>(Transport(clientReadsHere[0], clientWritesHere[1]), eventLoop));
    }

    // A fake adapter: answers every request the panel or Manager issues,
    // by command, until nothing is pending. Returns how many it served, so
    // a test can assert that something was actually asked.
    int ServeAll() {
        int served = 0;
        // The first frame of a batch is worth waiting for; once one has
        // arrived the rest follow immediately, so the wait drops.
        while (const std::optional<Json> request = reader.TryNext(served == 0 ? 500 : 20)) {
            const std::string command = request->value("command", "");
            const int         seq     = request->value("seq", 0);
            const Json&       args    = (*request)["arguments"];
            served += 1;
            lastRequests[command] = *request;
            if (command == "threads") {
                client->DispatchFrame(ResponseFrame(
                    seq, command, true,
                    Json{{"threads", Json::array({Json{{"id", 1}, {"name", "main"}}, Json{{"id", 2}, {"name", "worker"}}})}}));
            }
            else if (command == "stackTrace") {
                client->DispatchFrame(ResponseFrame(seq, command, true, Json{{"stackFrames", stackFrames}}));
            }
            else if (command == "scopes") {
                const int frameId = args.value("frameId", 0);
                client->DispatchFrame(ResponseFrame(
                    seq, command, true,
                    Json{{"scopes", frameId == 12 ? Json::array({Json{{"name", "Arguments"}, {"variablesReference", 300}}})
                                                  : Json::array({Json{{"name", "Locals"}, {"variablesReference", 100}}})}}));
            }
            else if (command == "variables") {
                const int reference = args.value("variablesReference", 0);
                Json      values    = Json::array();
                if (reference == 100) {
                    values = Json::array({Json{{"name", "count"}, {"value", "3"}},
                                          Json{{"name", "node"}, {"value", "Node * 0x55"}, {"variablesReference", 200}}});
                }
                else if (reference == 200) {
                    values = Json::array({Json{{"name", "next"}, {"value", "0x0"}}, Json{{"name", "key"}, {"value", "\"a\""}}});
                }
                else if (reference == 300) {
                    values = Json::array({Json{{"name", "argc"}, {"value", "1"}}});
                }
                client->DispatchFrame(ResponseFrame(seq, command, true, Json{{"variables", std::move(values)}}));
            }
            else if (command == "evaluate") {
                evaluated.push_back(args.value("expression", ""));
                client->DispatchFrame(ResponseFrame(seq, command, true, Json{{"result", "6"}}));
            }
            else if (command == "loadedSources") {
                client->DispatchFrame(ResponseFrame(
                    seq, command, true,
                    Json{{"sources", Json::array({Json{{"name", "parse.c"}, {"path", "/tmp/src/parse.c"}},
                                                  Json{{"name", "<generated>"}}})}}));
            }
            else if (command == "modules") {
                client->DispatchFrame(ResponseFrame(
                    seq, command, true,
                    Json{{"modules", Json::array({Json{{"id", 1},
                                                       {"name", "a.out"},
                                                       {"path", "/tmp/a.out"},
                                                       {"symbolStatus", "Symbols loaded."},
                                                       {"isUserCode", true}},
                                                  Json{{"id", "libc"}, {"name", "libc.so.6"}, {"symbolStatus", "No symbols."}}})}}));
            }
            else if (command == "setVariable") {
                client->DispatchFrame(ResponseFrame(seq, command, true, Json{{"value", args.value("value", "")}}));
            }
            else if (command == "initialize") {
                client->DispatchFrame(ResponseFrame(seq, command, true,
                                                    Json{{"supportsModulesRequest", advertiseInventory},
                                                         {"supportsLoadedSourcesRequest", advertiseInventory}}));
            }
            else {
                client->DispatchFrame(ResponseFrame(seq, command, true));
            }
        }
        return served;
    }

    // What the fake adapter answers a stackTrace with -- the second frame's
    // own scope differs (see ServeAll), which is how the frame-selection
    // test tells them apart.
    Json stackFrames =
        Json::array({Json{{"id", 11}, {"name", "parse"}, {"line", 42}, {"source", Json{{"path", "/tmp/parse.c"}}}},
                     Json{{"id", 12}, {"name", "main"}, {"line", 7}, {"source", Json{{"path", "/tmp/main.c"}}}}});
    std::map<std::string, Json> lastRequests;
    std::vector<std::string>    evaluated;
    // Set before StartRunningSession to model an adapter that implements
    // the two optional inventory requests.
    bool advertiseInventory = false;

    // Stops the debuggee and answers everything that follows.
    void StopAndServe() {
        client->DispatchFrame(EventFrame("stopped", Json{{"reason", "breakpoint"}, {"threadId", 1}}));
        REQUIRE(ServeAll() > 0);
        REQUIRE(manager.State() == Manager::SessionState::Stopped);
    }

    void StartRunningSession(const std::string& language) {
        SetLaunchConfig(language, R"({"program": "./fake-program"})");
        manager.StartOrContinue(language);
        REQUIRE(ServeAll() > 0); // initialize, then launch, then configurationDone
        REQUIRE(manager.State() == Manager::SessionState::Running);
        SetLaunchConfig(language, "");
    }

    ~Fixture() {
        if (adapterStdoutWrite >= 0) {
            ::close(adapterStdoutWrite);
        }
        if (adapterStdinRead >= 0) {
            ::close(adapterStdinRead);
        }
    }

    // Answers whatever prompt the panel last asked for.
    void AnswerPrompt(const std::string& text) {
        REQUIRE(promptAccept);
        auto accept = std::exchange(promptAccept, nullptr);
        accept(text);
    }

    void Paint() {
        panel.Tree().Paint(Canvas(screen, panel.Tree().Box_()));
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

    // The whole painted interior as one string -- most assertions here are
    // "is this row present at all", not "which row is it on".
    [[nodiscard]] std::string AllRows() {
        std::string text;
        for (int y = 1; y < kHeight - 1; ++y) {
            text += RowText(y) + "\n";
        }
        return text;
    }

    // Moves the selection down n times -- the panel's own key actions all
    // act on the selected row.
    void SelectDown(int n) {
        for (int i = 0; i < n; ++i) {
            REQUIRE(panel.Tree().OnEvent(ned::ui::test::ArrowDown()));
        }
    }

    // Selects the first row whose painted text contains `text`. Selecting
    // by content rather than by a counted number of ArrowDowns keeps these
    // tests from breaking every time a section is added above another --
    // which is exactly what happened while this panel was being built.
    void Select(const std::string& text) {
        Paint();
        int target = -1;
        for (int y = 1; y < kHeight - 1; ++y) {
            if (RowText(y).find(text) != std::string::npos) {
                target = y - 1; // row 0 of the model is painted at y = 1
                break;
            }
        }
        INFO(AllRows());
        REQUIRE(target >= 0);
        // kHeight holds every row these fixtures build, so nothing scrolls
        // and a painted y maps straight to a model index. Moved relative to
        // wherever the selection currently is -- a refresh may have left it
        // anywhere.
        const auto current = static_cast<int>(panel.Tree().SelectedRow().value_or(0));
        for (int i = current; i < target; ++i) {
            REQUIRE(panel.Tree().OnEvent(ned::ui::test::ArrowDown()));
        }
        for (int i = current; i > target; --i) {
            REQUIRE(panel.Tree().OnEvent(ned::ui::test::ArrowUp()));
        }
        REQUIRE(panel.Tree().SelectedRow() == static_cast<std::size_t>(target));
    }
};

std::filesystem::path TestPath(const std::string& name) {
    return std::filesystem::current_path() / name;
}

} // namespace

TEST_CASE("DebugPanel shows every section with no session running at all", "[DebugPanel]") {
    Fixture fixture;
    fixture.Paint();

    const std::string rows = fixture.AllRows();
    REQUIRE(rows.find("Breakpoints") != std::string::npos);
    REQUIRE(rows.find("Function breakpoints") != std::string::npos);
    REQUIRE(rows.find("Data breakpoints") != std::string::npos);
    REQUIRE(rows.find("Exception breakpoints") != std::string::npos);
    REQUIRE(rows.find("(none)") != std::string::npos); // empty sections say so rather than vanishing
}

TEST_CASE("DebugPanel groups source breakpoints under their file and shows their qualifiers", "[DebugPanel]") {
    Fixture    fixture;
    const auto path = TestPath("debug-panel-alpha.c");
    fixture.manager.ToggleBreakpoint(path, 12);
    fixture.manager.ToggleBreakpoint(path, 30);
    fixture.manager.SetBreakpointCondition(path, 30, "i > 3");
    fixture.Paint();

    const std::string rows = fixture.AllRows();
    REQUIRE(rows.find("debug-panel-alpha.c") != std::string::npos);
    REQUIRE(rows.find("12") != std::string::npos);
    REQUIRE(rows.find("30  if i > 3") != std::string::npos);
    // The section header carries the total, so a collapsed section still
    // says how much is under it.
    fixture.Select("Breakpoints");
    REQUIRE(fixture.RowText(static_cast<int>(*fixture.panel.Tree().SelectedRow()) + 1).find("2") != std::string::npos);
}

TEST_CASE("DebugPanel Space disables the selected breakpoint without removing it", "[DebugPanel]") {
    Fixture    fixture;
    const auto path = TestPath("debug-panel-toggle.c");
    fixture.manager.ToggleBreakpoint(path, 7);

    std::string message;
    fixture.panel.SetOnMessage([&](std::string m) { message = std::move(m); });

    fixture.Select("7");
    REQUIRE(fixture.panel.Tree().OnEvent(ned::ui::test::Character(" ")));
    REQUIRE(message == "Breakpoint disabled.");

    const auto stored = fixture.manager.BreakpointsForKey(Manager::NormalizePathKey(path));
    REQUIRE(stored.size() == 1);
    REQUIRE_FALSE(stored[0].enabled);

    // Said in words as well as in colour, because the selection brush wins
    // over a row's own colours -- and the selected row is the one being
    // acted on.
    fixture.Paint();
    REQUIRE(fixture.AllRows().find("off") != std::string::npos);

    REQUIRE(fixture.panel.Tree().OnEvent(ned::ui::test::Character(" ")));
    REQUIRE(message == "Breakpoint enabled.");
    REQUIRE(fixture.manager.BreakpointsForKey(Manager::NormalizePathKey(path))[0].enabled);
    fixture.Paint();
    REQUIRE(fixture.AllRows().find("off") == std::string::npos);
}

TEST_CASE("DebugPanel 'd' removes the selected breakpoint and the row goes with it", "[DebugPanel]") {
    Fixture    fixture;
    const auto path = TestPath("debug-panel-remove.c");
    fixture.manager.ToggleBreakpoint(path, 4);
    fixture.manager.ToggleBreakpoint(path, 9);

    fixture.Select("4");
    REQUIRE(fixture.panel.Tree().OnEvent(ned::ui::test::Character("d")));
    REQUIRE(fixture.manager.BreakpointsForFile(path) == std::vector<std::size_t>{9});

    fixture.Paint();
    const std::string rows = fixture.AllRows();
    REQUIRE(rows.find("debug-panel-remove.c") != std::string::npos);
    REQUIRE(rows.find("9") != std::string::npos);
}

TEST_CASE("DebugPanel 'd' on a file row removes every breakpoint in that file", "[DebugPanel]") {
    Fixture    fixture;
    const auto path = TestPath("debug-panel-file-remove.c");
    fixture.manager.ToggleBreakpoint(path, 2);
    fixture.manager.ToggleBreakpoint(path, 5);
    fixture.manager.ToggleBreakpoint(path, 8);

    fixture.Select("debug-panel-file-remove.c");
    REQUIRE(fixture.panel.Tree().OnEvent(ned::ui::test::Character("d")));
    REQUIRE(fixture.manager.AllBreakpoints().empty());
}

TEST_CASE("DebugPanel 'X' clears the whole section the selection sits in", "[DebugPanel]") {
    Fixture    fixture;
    const auto path = TestPath("debug-panel-clear.c");
    fixture.manager.ToggleBreakpoint(path, 3);
    fixture.manager.ToggleFunctionBreakpoint("main");

    fixture.Select("3");
    REQUIRE(fixture.panel.Tree().OnEvent(ned::ui::test::Character("X")));
    REQUIRE(fixture.manager.AllBreakpoints().empty());
    // The other section is untouched -- X is per section, not global.
    REQUIRE(fixture.manager.FunctionBreakpoints().size() == 1);
}

TEST_CASE("DebugPanel Enter on a breakpoint reports the file and line to visit", "[DebugPanel]") {
    Fixture    fixture;
    const auto path = TestPath("debug-panel-visit.c");
    fixture.manager.ToggleBreakpoint(path, 21);

    std::filesystem::path visitedPath;
    std::size_t           visitedLine = 0;
    fixture.panel.SetOnVisitLocation([&](const std::filesystem::path& p, std::size_t line) {
        visitedPath = p;
        visitedLine = line;
    });

    fixture.Select("21");
    REQUIRE(fixture.panel.Tree().OnEvent(ned::ui::test::Return()));
    REQUIRE(visitedPath == std::filesystem::path(Manager::NormalizePathKey(path)));
    REQUIRE(visitedLine == 21);
}

TEST_CASE("DebugPanel 'c' sets a condition on the selected breakpoint through a prompt", "[DebugPanel]") {
    Fixture    fixture;
    const auto path = TestPath("debug-panel-prompt.c");
    fixture.manager.ToggleBreakpoint(path, 15);

    fixture.Select("15");
    REQUIRE(fixture.panel.Tree().OnEvent(ned::ui::test::Character("c")));
    REQUIRE(fixture.promptLabel == "Condition (empty to clear)");
    REQUIRE(fixture.promptInitial.empty()); // no condition yet
    fixture.AnswerPrompt("i == 3");
    REQUIRE(fixture.manager.BreakpointsForKey(Manager::NormalizePathKey(path))[0].condition == "i == 3");

    // Asked again, the prompt is pre-filled with what is already set --
    // editing a condition must not mean retyping it.
    REQUIRE(fixture.panel.Tree().OnEvent(ned::ui::test::Character("c")));
    REQUIRE(fixture.promptInitial == "i == 3");
    fixture.AnswerPrompt(""); // empty clears
    REQUIRE(fixture.manager.BreakpointsForKey(Manager::NormalizePathKey(path))[0].condition.empty());

    // 'a' needs no selected row at all -- it names a function, not a line.
    REQUIRE(fixture.panel.Tree().OnEvent(ned::ui::test::Character("a")));
    REQUIRE(fixture.promptLabel == "Function breakpoint name");
    fixture.AnswerPrompt("main");
    REQUIRE(fixture.manager.FunctionBreakpoints().size() == 1);
}

TEST_CASE("DebugPanel collapses a section and keeps it collapsed across a refresh", "[DebugPanel]") {
    Fixture    fixture;
    const auto path = TestPath("debug-panel-collapse.c");
    fixture.manager.ToggleBreakpoint(path, 6);

    fixture.Select("Breakpoints");
    REQUIRE(fixture.panel.Tree().OnEvent(ned::ui::test::ArrowLeft()));
    fixture.Paint();
    REQUIRE(fixture.AllRows().find("debug-panel-collapse.c") == std::string::npos);

    // A store change elsewhere rebuilds every row -- the collapse must
    // survive it, or a stop event would re-open everything.
    fixture.manager.ToggleBreakpoint(path, 11);
    fixture.Paint();
    REQUIRE(fixture.AllRows().find("debug-panel-collapse.c") == std::string::npos);

    REQUIRE(fixture.panel.Tree().OnEvent(ned::ui::test::ArrowRight()));
    fixture.Paint();
    REQUIRE(fixture.AllRows().find("debug-panel-collapse.c") != std::string::npos);
    // The count on the collapsed header still reported what was under it.
    REQUIRE(fixture.manager.BreakpointsForFile(path) == std::vector<std::size_t>{6, 11});
}

TEST_CASE("DebugPanel keeps the selection on the same breakpoint when one is added above it", "[DebugPanel]") {
    Fixture    fixture;
    const auto path = TestPath("debug-panel-selection.c");
    fixture.manager.ToggleBreakpoint(path, 40);

    fixture.Select("40");
    fixture.manager.ToggleBreakpoint(path, 10); // sorts above it, shifting every index down

    std::string message;
    fixture.panel.SetOnMessage([&](std::string m) { message = std::move(m); });
    REQUIRE(fixture.panel.Tree().OnEvent(ned::ui::test::Character("d")));

    // Line 40 is what was selected, so line 40 is what was removed.
    REQUIRE(fixture.manager.BreakpointsForFile(path) == std::vector<std::size_t>{10});
    REQUIRE(message == "Breakpoint removed.");
}

TEST_CASE("DebugPanel lists function breakpoints and toggles them with Space", "[DebugPanel]") {
    Fixture fixture;
    fixture.manager.ToggleFunctionBreakpoint("parse_expression");

    fixture.Paint();
    REQUIRE(fixture.AllRows().find("parse_expression") != std::string::npos);

    fixture.Select("parse_expression");
    REQUIRE(fixture.panel.Tree().OnEvent(ned::ui::test::Character(" ")));
    REQUIRE_FALSE(fixture.manager.FunctionBreakpoints()[0].enabled);
}

TEST_CASE("DebugPanel says so rather than acting on a row with nothing to do", "[DebugPanel]") {
    Fixture     fixture;
    std::string message;
    fixture.panel.SetOnMessage([&](std::string m) { message = std::move(m); });

    // A section header: enable/disable means nothing there.
    REQUIRE(fixture.panel.Tree().OnEvent(ned::ui::test::Character(" ")));
    REQUIRE(message == "Nothing to enable or disable on this row.");

    message.clear();
    REQUIRE(fixture.panel.Tree().OnEvent(ned::ui::test::Character("c")));
    REQUIRE(message == "Nothing to edit on this row.");

    // 'h'/'l' are breakpoint-only and say which row they wanted.
    message.clear();
    REQUIRE(fixture.panel.Tree().OnEvent(ned::ui::test::Character("h")));
    REQUIRE(message == "Select a source breakpoint first.");
}

// ------------------------------------------------- the session-scoped half
//
// These need a live adapter. Fixture::ServeAll answers every request by
// command rather than by position: the panel fans its requests out and they
// interleave with Manager's own stop handling, so an order assertion here
// would be asserting an implementation detail rather than behaviour.

TEST_CASE("DebugPanel shows the stack, its scopes and its variables once stopped", "[DebugPanel]") {
    Fixture fixture;
    fixture.InjectClient();
    fixture.StartRunningSession("debug-panel-test-stack");
    fixture.StopAndServe();
    fixture.Paint();

    const std::string rows = fixture.AllRows();
    INFO(rows);
    REQUIRE(rows.find("Call stack") != std::string::npos);
    REQUIRE(rows.find("worker") != std::string::npos); // the other thread, listed but unexpanded
    REQUIRE(rows.find("parse") != std::string::npos);  // the stopped thread's frames came without asking
    REQUIRE(rows.find("parse.c:42") != std::string::npos);
    REQUIRE(rows.find("Locals") != std::string::npos);
    REQUIRE(rows.find("count") != std::string::npos);
    REQUIRE(rows.find("Node * 0x55") != std::string::npos);
    // The other thread's frames cost a request, so they were not fetched.
    REQUIRE(rows.find("main.c:7") != std::string::npos); // the stopped thread's own second frame
}

TEST_CASE("DebugPanel expands a composite variable on demand", "[DebugPanel]") {
    Fixture fixture;
    fixture.InjectClient();
    fixture.StartRunningSession("debug-panel-test-expand");
    fixture.StopAndServe();

    fixture.Paint();
    REQUIRE(fixture.AllRows().find("next") == std::string::npos); // not fetched until asked

    fixture.Select("node");
    REQUIRE(fixture.panel.Tree().OnEvent(ned::ui::test::ArrowRight()));
    REQUIRE(fixture.ServeAll() > 0);
    REQUIRE(fixture.lastRequests.at("variables")["arguments"]["variablesReference"] == 200);

    fixture.Paint();
    const std::string rows = fixture.AllRows();
    INFO(rows);
    REQUIRE(rows.find("next") != std::string::npos);
    REQUIRE(rows.find("key") != std::string::npos);
}

TEST_CASE("DebugPanel expands another thread's frames only when asked", "[DebugPanel]") {
    Fixture fixture;
    fixture.InjectClient();
    fixture.StartRunningSession("debug-panel-test-thread");
    fixture.StopAndServe();

    fixture.Select("worker");
    REQUIRE(fixture.panel.Tree().OnEvent(ned::ui::test::ArrowRight()));
    REQUIRE(fixture.ServeAll() > 0);
    // Asked about thread 2 specifically -- without changing which thread a
    // following step or continue would target.
    REQUIRE(fixture.lastRequests.at("stackTrace")["arguments"]["threadId"] == 2);
    REQUIRE(fixture.manager.FocusedThreadId() == 1);
}

TEST_CASE("DebugPanel selecting a frame re-scopes the variables to it", "[DebugPanel]") {
    Fixture fixture;
    fixture.InjectClient();
    fixture.StartRunningSession("debug-panel-test-frame");
    fixture.StopAndServe();

    std::filesystem::path visitedPath;
    std::size_t           visitedLine = 0;
    fixture.panel.SetOnVisitLocation([&](const std::filesystem::path& p, std::size_t line) {
        visitedPath = p;
        visitedLine = line;
    });

    fixture.Select("main.c:7"); // the second frame
    REQUIRE(fixture.panel.Tree().OnEvent(ned::ui::test::Return()));
    REQUIRE(fixture.ServeAll() > 0);

    // Enter both jumps to the frame's source and re-points every evaluation
    // at it.
    REQUIRE(visitedPath == std::filesystem::path("/tmp/main.c"));
    REQUIRE(visitedLine == 7);
    REQUIRE(fixture.manager.FocusedFrameId() == 12);
    REQUIRE(fixture.lastRequests.at("scopes")["arguments"]["frameId"] == 12);

    fixture.Paint();
    const std::string rows = fixture.AllRows();
    INFO(rows);
    REQUIRE(rows.find("Arguments") != std::string::npos);
    REQUIRE(rows.find("argc") != std::string::npos);
    REQUIRE(rows.find("Locals") == std::string::npos); // the previous frame's scope is gone, not stacked
}

TEST_CASE("DebugPanel evaluates watches against the stop and drops the values on resume", "[DebugPanel]") {
    Fixture fixture;
    fixture.InjectClient();
    fixture.manager.AddWatch("count * 2");
    fixture.StartRunningSession("debug-panel-test-watch");
    fixture.StopAndServe();

    REQUIRE(std::find(fixture.evaluated.begin(), fixture.evaluated.end(), "count * 2") != fixture.evaluated.end());
    fixture.Paint();
    INFO(fixture.AllRows());
    REQUIRE(fixture.AllRows().find("count * 2") != std::string::npos);
    REQUIRE(fixture.AllRows().find("6") != std::string::npos);

    // Continuing invalidates every value -- the expression stays, its value
    // does not, and neither does the stack.
    fixture.manager.StartOrContinue("debug-panel-test-watch");
    fixture.ServeAll();
    REQUIRE(fixture.manager.State() == Manager::SessionState::Running);

    fixture.Paint();
    const std::string rows = fixture.AllRows();
    INFO(rows);
    REQUIRE(rows.find("count * 2") != std::string::npos); // still listed
    REQUIRE(rows.find("Call stack") == std::string::npos);
    REQUIRE(rows.find("Locals") == std::string::npos);
}

TEST_CASE("DebugPanel 'w' on a variable watches it by name", "[DebugPanel]") {
    Fixture fixture;
    fixture.InjectClient();
    fixture.StartRunningSession("debug-panel-test-watch-variable");
    fixture.StopAndServe();

    fixture.Select("count");
    REQUIRE(fixture.panel.Tree().OnEvent(ned::ui::test::Character("w")));
    REQUIRE(fixture.manager.Watches() == std::vector<std::string>{"count"});
    REQUIRE(fixture.ServeAll() > 0);
    REQUIRE(std::find(fixture.evaluated.begin(), fixture.evaluated.end(), "count") != fixture.evaluated.end());
}

TEST_CASE("DebugPanel 'c' on a variable sets its value through the adapter", "[DebugPanel]") {
    Fixture fixture;
    fixture.InjectClient();
    fixture.StartRunningSession("debug-panel-test-set-variable");
    fixture.StopAndServe();

    fixture.Select("count");
    REQUIRE(fixture.panel.Tree().OnEvent(ned::ui::test::Character("c")));
    REQUIRE(fixture.promptLabel == "New value for count");
    REQUIRE(fixture.promptInitial == "3"); // pre-filled with what it is now
    fixture.AnswerPrompt("9");
    REQUIRE(fixture.ServeAll() > 0);

    const Json& setVariable = fixture.lastRequests.at("setVariable");
    REQUIRE(setVariable["arguments"]["variablesReference"] == 100); // the CONTAINER, not the variable
    REQUIRE(setVariable["arguments"]["name"] == "count");
    REQUIRE(setVariable["arguments"]["value"] == "9");
}

TEST_CASE("DebugPanel lists loaded sources and modules when the adapter implements them", "[DebugPanel]") {
    Fixture fixture;
    fixture.InjectClient();
    fixture.advertiseInventory = true;
    fixture.StartRunningSession("debug-panel-test-inventory");
    fixture.StopAndServe();
    fixture.Paint();

    const std::string rows = fixture.AllRows();
    INFO(rows);
    REQUIRE(rows.find("Loaded sources") != std::string::npos);
    REQUIRE(rows.find("parse.c") != std::string::npos);
    REQUIRE(rows.find("no file") != std::string::npos); // the generated source has no path
    REQUIRE(rows.find("Modules") != std::string::npos);
    REQUIRE(rows.find("libc.so.6") != std::string::npos);
    REQUIRE(rows.find("No symbols.") != std::string::npos); // why a breakpoint in there wouldn't bind

    std::filesystem::path visited;
    fixture.panel.SetOnVisitLocation([&](const std::filesystem::path& p, std::size_t) { visited = p; });
    fixture.Select("parse.c   ");
    REQUIRE(fixture.panel.Tree().OnEvent(ned::ui::test::Return()));
    REQUIRE(visited == std::filesystem::path("/tmp/src/parse.c"));
}

TEST_CASE("DebugPanel omits the inventory sections entirely for an adapter without them", "[DebugPanel]") {
    Fixture fixture;
    fixture.InjectClient();
    fixture.StartRunningSession("debug-panel-test-no-inventory"); // advertiseInventory stays false
    fixture.StopAndServe();
    fixture.Paint();

    const std::string rows = fixture.AllRows();
    INFO(rows);
    // Not an empty section -- an empty one would claim the debuggee loaded
    // nothing, when the truth is this adapter cannot answer.
    REQUIRE(rows.find("Loaded sources") == std::string::npos);
    REQUIRE(rows.find("Modules") == std::string::npos);
    REQUIRE(fixture.lastRequests.find("loadedSources") == fixture.lastRequests.end());
    REQUIRE(fixture.lastRequests.find("modules") == fixture.lastRequests.end());
}
