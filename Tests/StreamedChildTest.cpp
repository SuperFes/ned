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

TEST_CASE("StreamedChild feeds input while it reads output", "[Process]") {
    ned::ui::EventLoop eventLoop;
    std::string        input(4 * 1024 * 1024, 'x');
    for (std::size_t i = 0; i < input.size(); i += 4096) {
        input[i] = static_cast<char>('a' + i / 4096 % 26);
    }
    std::string        output;
    std::optional<int> exitCode;
    bool               exited = false;

    // cat writes as it reads, so writing everything before reading any of
    // it would fill both pipes and stall.
    StreamedChild child(ChildProcess({"cat"}), eventLoop, [&](std::string_view chunk) { output += chunk; }, [&](std::optional<int> code) {
        exited   = true;
        exitCode = code; });
    child.Feed(input, std::chrono::seconds(5));

    REQUIRE(DrainUntil(eventLoop, [&] { return exited; }));
    CHECK(output == input);
    CHECK(exitCode == 0);
}

TEST_CASE("StreamedChild kills a child that stops taking its input", "[Process]") {
    ned::ui::EventLoop eventLoop;
    std::optional<int> exitCode = 0;
    bool               exited   = false;

    StreamedChild child(ChildProcess({"sleep", "30"}), eventLoop, [](std::string_view) {}, [&](std::optional<int> code) {
        exited   = true;
        exitCode = code; });
    child.Feed(std::string(4 * 1024 * 1024, 'x'), std::chrono::milliseconds(100));

    REQUIRE(DrainUntil(eventLoop, [&] { return exited; }));
    CHECK_FALSE(exitCode.has_value());
}

TEST_CASE("StreamedChild's teardown ends a Feed still writing", "[Process]") {
    ned::ui::EventLoop eventLoop;
    const auto         started = std::chrono::steady_clock::now();
    {
        StreamedChild child(ChildProcess({"sleep", "30"}), eventLoop, [](std::string_view) {}, [](std::optional<int>) {});
        child.Feed(std::string(4 * 1024 * 1024, 'x'), std::chrono::seconds(30));
    }
    CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds(10));
}

TEST_CASE("StreamedChild sends queued input in order while it reads output", "[Process]") {
    ned::ui::EventLoop eventLoop;
    std::string        output;
    std::string        sent;

    StreamedChild child(ChildProcess({"cat"}), eventLoop, [&](std::string_view chunk) { output += chunk; }, [](std::optional<int>) {});
    for (int i = 0; i < 64; ++i) {
        const std::string piece(64 * 1024, static_cast<char>('a' + i % 26));
        sent += piece;
        child.Send(piece);
    }

    REQUIRE(DrainUntil(eventLoop, [&] { return output.size() >= sent.size(); }));
    CHECK(output == sent);
}

TEST_CASE("StreamedChild's Send never waits on a child not reading, and teardown ends it", "[Process]") {
    ned::ui::EventLoop eventLoop;
    const auto         started = std::chrono::steady_clock::now();
    {
        StreamedChild child(ChildProcess({"sleep", "30"}), eventLoop, [](std::string_view) {}, [](std::optional<int>) {});
        child.Send(std::string(4 * 1024 * 1024, 'x'));
        child.Send("more");
        CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds(2));
    }
    CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds(10));
}
