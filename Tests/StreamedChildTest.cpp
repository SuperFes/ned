#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "Editor/Process/StreamedChild.h"
#include "UI/EventLoop.h"

using ned::editor::process::ChildProcess;
using ned::editor::process::StreamedChild;

namespace {

template <typename Done>
bool DrainUntil(ned::ui::EventLoop& eventLoop, Done done) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!done()) {
        if (std::chrono::steady_clock::now() > deadline) {
            return false;
        }
        if (!eventLoop.DrainPosted_()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    return true;
}

} // namespace

TEST_CASE("StreamedChild delivers all output, then the exit code, on the loop", "[Process]") {
    ned::ui::EventLoop       eventLoop;
    std::vector<std::string> events;
    std::string              output;

    StreamedChild child(
        ChildProcess({"sh", "-c", "printf one; printf two; exit 3"}), eventLoop,
        [&](std::string_view chunk) {
            output += chunk;
            events.emplace_back("output");
        },
        [&](std::optional<int> exitCode) { events.push_back("exit:" + (exitCode ? std::to_string(*exitCode) : "signal")); });

    REQUIRE(DrainUntil(eventLoop, [&] { return !events.empty() && events.back().starts_with("exit:"); }));
    CHECK(output == "onetwo");
    CHECK(events.back() == "exit:3");
    CHECK(std::count(events.begin(), events.end(), "exit:3") == 1);
}

TEST_CASE("StreamedChild reports a killed child's exit as a signal", "[Process]") {
    ned::ui::EventLoop eventLoop;
    bool               exited = false;
    std::optional<int> exitCode;

    StreamedChild child(ChildProcess({"sleep", "30"}), eventLoop, [](std::string_view) {}, [&](std::optional<int> code) {
        exited   = true;
        exitCode = code; });
    child.Child().Kill();

    REQUIRE(DrainUntil(eventLoop, [&] { return exited; }));
    CHECK_FALSE(exitCode.has_value());
}
