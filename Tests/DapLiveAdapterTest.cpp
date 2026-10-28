#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "Editor/Dap/Config.h"
#include "Editor/Dap/Manager.h"
#include "Editor/Process/ChildProcess.h"
#include "FakeRemoteHost.h"
#include "Text/RemoteFile.h"
#include "UI/EventLoop.h"

namespace fs   = std::filesystem;
namespace dap  = ned::editor::dap;
namespace text = ned::text;
using Json     = nlohmann::json;
using ned::tests::PumpUntil;

namespace {

// Line 5 calls two functions, so a stop there has two step-in targets, and
// `total` is assignable.
constexpr const char* kCSource = "static int square(int x) { return x * x; }\n"
                                 "static int twice(int x) { return x + x; }\n"
                                 "int main(void) {\n"
                                 "    int total = 3;\n"
                                 "    total = square(total) + twice(total);\n"
                                 "    return total;\n"
                                 "}\n";

constexpr const char* kPythonSource = "def square(x):\n"
                                      "    return x * x\n"
                                      "def twice(x):\n"
                                      "    return x + x\n"
                                      "total = 3\n"
                                      "total = square(total) + twice(total)\n"
                                      "print(total)\n";

// What an adapter is expected to answer. A request it never advertised must
// come back empty rather than fail, so each flag is checked both ways.
struct Adapter {
    std::string              language;
    std::vector<std::string> argv;
    fs::path                 source;
    Json                     launch;
    std::size_t              line = 0; // the breakpoint, on the two-call line
    // What the console's input starts with to name an expression: gdb's
    // repl is its own CLI.
    std::string replPrefix;
    bool        modules;
    bool        loadedSources;
    bool        breakpointLocations;
    bool        stepInTargets;
    bool        setExpression;
    bool        completions;
};

void Run(const std::vector<std::string>& argv) {
    ned::editor::process::ChildProcess process(argv, ned::editor::process::StderrMode::MergeWithStdout);
    std::string                        output;
    for (std::string chunk = process.ReadSome(); !chunk.empty(); chunk = process.ReadSome()) {
        output += chunk;
    }
    INFO(output);
    REQUIRE(process.WaitForExit() == std::optional<int>(0));
}

fs::path CompileC(const fs::path& dir) {
    const fs::path source = dir / "prog.c";
    std::ofstream(source) << kCSource;
    Run({"cc", "-g", "-O0", "-o", (dir / "prog").string(), source.string()});
    return source;
}

bool PythonHasDebugpy(const std::string& python) {
    try {
        ned::editor::process::ChildProcess probe({python, "-c", "import debugpy"}, ned::editor::process::StderrMode::Discard);
        while (!probe.ReadSome().empty()) {
        }
        return probe.WaitForExit() == std::optional<int>(0);
    }
    catch (const std::runtime_error&) {
        return false;
    }
}

template <typename T, typename Request>
T Await(ned::ui::EventLoop& loop, Request request) {
    std::optional<T> answer;
    request([&](T value) { answer = std::move(value); });
    REQUIRE(PumpUntil(loop, [&] { return answer.has_value(); }));
    return std::move(*answer);
}

std::string EvaluateValue(ned::ui::EventLoop& loop, dap::Manager& manager, const std::string& expression) {
    std::optional<std::string> value;
    manager.Evaluate(expression, [&](bool success, std::string result) {
        value = success ? std::move(result) : "<failed: " + result + ">";
    });
    REQUIRE(PumpUntil(loop, [&] { return value.has_value(); }));
    return *value;
}

// Stops at adapter.line, puts each of the six capability-gated requests to
// the real adapter, then ends the session.
void Exercise(const Adapter& adapter) {
    struct Restore {
        std::string language;
        ~Restore() {
            dap::SetAdapterCommand(language, {});
            dap::SetLaunchConfig(language, "");
        }
    } restore{adapter.language};
    dap::SetAdapterCommand(adapter.language, adapter.argv);
    dap::SetLaunchConfig(adapter.language, adapter.launch.dump());

    ned::ui::EventLoop                       loop;
    dap::Manager                             manager{loop};
    std::optional<dap::Manager::StoppedInfo> stopped;
    std::optional<std::string>               ended;
    manager.SetOnStopped([&](const dap::Manager::StoppedInfo& info) { stopped = info; });
    manager.SetOnSessionEnded([&](std::string reason) { ended = std::move(reason); });
    const text::FileRef file{adapter.source};
    manager.ToggleBreakpoint(file, adapter.line);

    manager.StartOrContinue(adapter.language);
    REQUIRE(PumpUntil(loop, [&] { return stopped.has_value() || ended.has_value(); }, std::chrono::seconds(60)));
    INFO(ended.value_or(""));
    REQUIRE(stopped.has_value());
    REQUIRE(stopped->line == adapter.line);

    const auto frames = Await<std::vector<dap::Manager::StackFrame>>(loop, [&](auto done) { manager.RequestStackTrace(done); });
    REQUIRE_FALSE(frames.empty());

    const auto modules = Await<std::vector<dap::Manager::Module>>(loop, [&](auto done) { manager.RequestModules(done); });
    CHECK(modules.empty() != adapter.modules);

    const auto sources = Await<std::vector<dap::Manager::LoadedSource>>(loop, [&](auto done) { manager.RequestLoadedSources(done); });
    if (adapter.loadedSources) {
        CHECK(std::ranges::any_of(sources, [&](const dap::Manager::LoadedSource& source) { return source.file == std::optional(file); }));
    }
    else {
        CHECK(sources.empty());
    }

    const auto lines = Await<std::vector<std::size_t>>(loop, [&](auto done) {
        manager.RequestBreakpointLocations(file, adapter.line - 1, adapter.line + 1, done);
    });
    if (adapter.breakpointLocations) {
        CHECK(std::ranges::contains(lines, adapter.line));
        CHECK(std::ranges::all_of(lines, [&](std::size_t line) { return line + 1 >= adapter.line && line <= adapter.line + 1; }));
    }
    else {
        CHECK(lines.empty());
    }

    const auto targets = Await<std::vector<dap::Manager::StepInTarget>>(loop, [&](auto done) {
        manager.RequestStepInTargets(frames.front().id, done);
    });
    if (adapter.stepInTargets) {
        const auto labelled = [&](const char* name) {
            return std::ranges::any_of(targets, [&](const dap::Manager::StepInTarget& target) { return target.label.contains(name); });
        };
        CHECK(labelled("square"));
        CHECK(labelled("twice"));
    }
    else {
        CHECK(targets.empty());
    }

    const auto assigned = Await<dap::Manager::SetVariableResult>(loop, [&](auto done) { manager.SetExpression("total", "7", done); });
    INFO(assigned.errorMessage);
    CHECK(assigned.success == adapter.setExpression);
    // gdb's CLI answers "$1 = 7\n".
    std::string value = EvaluateValue(loop, manager, adapter.replPrefix + "total");
    value.erase(value.find_last_not_of(" \n") + 1);
    CHECK(value.ends_with(adapter.setExpression ? "7" : "3"));

    // "tot" completed at its end: the column is 1-based, one past the text.
    const std::string typed       = adapter.replPrefix + "tot";
    const auto        completions = Await<std::vector<dap::Manager::Completion>>(loop, [&](auto done) {
        manager.RequestCompletions(typed, static_cast<int>(typed.size()) + 1, done);
    });
    if (adapter.completions) {
        const auto total = std::ranges::find_if(completions, [](const dap::Manager::Completion& completion) {
            return completion.label.ends_with("total");
        });
        INFO(completions.size() << " completions");
        REQUIRE(total != completions.end());
        INFO("label " << total->label << " text " << total->text << " length " << total->length);
        CHECK(dap::ApplyCompletion(typed, typed.size(), *total) == adapter.replPrefix + "total");
    }
    else {
        CHECK(completions.empty());
    }

    manager.StopSession();
    REQUIRE(PumpUntil(loop, [&] { return ended.has_value(); }, std::chrono::seconds(30)));
}

} // namespace

TEST_CASE("The capability-gated requests answer from gdb's own DAP interpreter", "[Dap][DapLive][gdb]") {
    if (!ned::editor::process::ResolveExecutable("gdb") || !ned::editor::process::ResolveExecutable("cc")) {
        SKIP("gdb or cc not found on $PATH");
    }
    const fs::path dir    = ned::tests::FreshRemoteDir();
    const fs::path source = CompileC(dir);
    Exercise({.language            = "c",
              .argv                = {"gdb", "-q", "-nx", "-i=dap"},
              .source              = source,
              .launch              = {{"program", (dir / "prog").string()}, {"cwd", dir.string()}},
              .line                = 5,
              .replPrefix          = "print ",
              .modules             = true,
              .loadedSources       = true,
              .breakpointLocations = true,
              .stepInTargets       = false,
              .setExpression       = true,
              .completions         = true});
}

TEST_CASE("The capability-gated requests answer from lldb-dap", "[Dap][DapLive][lldb]") {
    if (!ned::editor::process::ResolveExecutable("lldb-dap") || !ned::editor::process::ResolveExecutable("cc")) {
        SKIP("lldb-dap or cc not found on $PATH");
    }
    const fs::path dir    = ned::tests::FreshRemoteDir();
    const fs::path source = CompileC(dir);
    Exercise({.language            = "c",
              .argv                = {"lldb-dap"},
              .source              = source,
              .launch              = {{"program", (dir / "prog").string()}, {"cwd", dir.string()}},
              .line                = 5,
              .modules             = true,
              .loadedSources       = false,
              .breakpointLocations = true,
              .stepInTargets       = true,
              .setExpression       = false,
              .completions         = true});
}

// NED_TEST_DEBUGPY_PYTHON names a Python with debugpy installed, typically a
// venv's, since distributions rarely package it.
TEST_CASE("The capability-gated requests answer from debugpy", "[Dap][DapLive][debugpy]") {
    const char*       configured = std::getenv("NED_TEST_DEBUGPY_PYTHON");
    const std::string python     = configured != nullptr && *configured != '\0' ? configured : "python3";
    if (!PythonHasDebugpy(python)) {
        SKIP("no debugpy in " + python + "; set NED_TEST_DEBUGPY_PYTHON");
    }
    const fs::path dir    = ned::tests::FreshRemoteDir();
    const fs::path source = dir / "prog.py";
    std::ofstream(source) << kPythonSource;
    Exercise({.language            = "python",
              .argv                = {python, "-m", "debugpy.adapter"},
              .source              = source,
              .launch              = {{"program", source.string()}, {"cwd", dir.string()}, {"console", "internalConsole"}},
              .line                = 6,
              .modules             = true,
              .loadedSources       = false,
              .breakpointLocations = false,
              .stepInTargets       = true,
              .setExpression       = true,
              .completions         = true});
}
