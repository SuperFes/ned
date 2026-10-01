#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <functional>
#include <optional>
#include <thread>

#include "Editor/Lifetime.h"
#include "UI/EventLoop.h"

using ned::editor::LifetimeGuard;
using ned::editor::LifetimeToken;

TEST_CASE("A bound callback runs while its guard is alive", "[Lifetime]") {
    LifetimeGuard guard;
    int           seen = 0;

    std::function<void(int)> callback = guard.Bind([&seen](int value) { seen = value; });
    callback(7);

    REQUIRE(seen == 7);
}

TEST_CASE("A bound callback does nothing once its guard is destroyed", "[Lifetime]") {
    std::optional<LifetimeGuard> guard(std::in_place);
    int                          calls = 0;

    std::function<void()> callback = guard->Bind([&calls] { ++calls; });
    guard.reset();
    callback();

    REQUIRE(calls == 0);
}

TEST_CASE("Revoke kills earlier tokens but not later ones", "[Lifetime]") {
    LifetimeGuard       guard;
    const LifetimeToken before = guard.Token();

    guard.Revoke();

    REQUIRE_FALSE(before.Alive());
    REQUIRE(guard.Token().Alive());
}

TEST_CASE("A mutable callback keeps its own state across calls", "[Lifetime]") {
    LifetimeGuard guard;
    int           last = 0;

    std::function<void()> callback = guard.Bind([&last, count = 0]() mutable { last = ++count; });
    callback();
    callback();

    REQUIRE(last == 2);
}

namespace {

// DeadlineTimer has no "fired" signal, so this gives a zero-delay fire time
// to reach the loop's queue.
void WaitForPost() {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

} // namespace

TEST_CASE("A cancelled DeadlineTimer drops a fire already posted to the loop", "[Lifetime][DeadlineTimer]") {
    ned::ui::EventLoop     loop;
    ned::ui::DeadlineTimer timer;
    int                    fired = 0;

    timer.Arm(loop, std::chrono::milliseconds(0), [&fired] { ++fired; });
    WaitForPost();
    timer.Cancel();
    (void)loop.DrainPosted_();

    REQUIRE(fired == 0);
}

TEST_CASE("A destroyed DeadlineTimer drops a fire already posted to the loop", "[Lifetime][DeadlineTimer]") {
    ned::ui::EventLoop loop;
    int                fired = 0;

    {
        ned::ui::DeadlineTimer timer;
        timer.Arm(loop, std::chrono::milliseconds(0), [&fired] { ++fired; });
        WaitForPost();
    }
    (void)loop.DrainPosted_();

    REQUIRE(fired == 0);
}

TEST_CASE("Re-arming a DeadlineTimer drops the earlier fire already posted", "[Lifetime][DeadlineTimer]") {
    ned::ui::EventLoop     loop;
    ned::ui::DeadlineTimer timer;
    int                    first  = 0;
    int                    second = 0;

    timer.Arm(loop, std::chrono::milliseconds(0), [&first] { ++first; });
    WaitForPost();
    timer.Arm(loop, std::chrono::milliseconds(0), [&second] { ++second; });
    WaitForPost();
    (void)loop.DrainPosted_();

    REQUIRE(first == 0);
    REQUIRE(second == 1);
}
