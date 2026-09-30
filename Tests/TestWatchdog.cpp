//
// Aborts ned_tests when one test case runs past a deadline, naming it. A hang
// otherwise shows up only as a run that never finishes, with nothing saying
// which test is stuck. NED_TEST_WATCHDOG_SECONDS overrides the deadline; 0
// turns the watchdog off, e.g. while stopped in a debugger.
//

#include <catch2/catch_test_case_info.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>

#include "TestWatchdog.h"

namespace ned::test {

std::optional<std::chrono::seconds> WatchdogLimit(const char* setting) {
    constexpr std::chrono::seconds kDefault{300};
    if (setting == nullptr || *setting == '\0') {
        return kDefault;
    }
    char*           end     = nullptr;
    const long long seconds = std::strtoll(setting, &end, 10);
    if (*end != '\0' || seconds < 0) {
        return kDefault;
    }
    if (seconds == 0) {
        return std::nullopt;
    }
    return std::chrono::seconds(seconds);
}

} // namespace ned::test

namespace {

class TestWatchdog final : public Catch::EventListenerBase {
  public:
    using Catch::EventListenerBase::EventListenerBase;

    void testRunStarting(const Catch::TestRunInfo& /*info*/) override {
        const std::optional<std::chrono::seconds> limit = ned::test::WatchdogLimit(std::getenv("NED_TEST_WATCHDOG_SECONDS"));
        if (limit) {
            watcher_ = std::jthread([this, limit = *limit](const std::stop_token& stop) { Watch(stop, limit); });
        }
    }

    void testCaseStarting(const Catch::TestCaseInfo& info) override {
        const std::lock_guard lock(mutex_);
        name_    = info.name;
        started_ = std::chrono::steady_clock::now();
        running_ = true;
    }

    void testCaseEnded(const Catch::TestCaseStats& /*stats*/) override {
        const std::lock_guard lock(mutex_);
        running_ = false;
    }

    void testRunEnded(const Catch::TestRunStats& /*stats*/) override {
        watcher_.request_stop();
        if (watcher_.joinable()) {
            watcher_.join();
        }
    }

  private:
    void Watch(const std::stop_token& stop, std::chrono::seconds limit) {
        std::unique_lock lock(mutex_);
        while (!stop.stop_requested()) {
            if (running_ && std::chrono::steady_clock::now() - started_ >= limit) {
                const std::string name = name_;
                lock.unlock();
                std::fprintf(stderr, "\nned_tests watchdog: \"%s\" has run for over %lld s, aborting\n", name.c_str(),
                             static_cast<long long>(limit.count()));
                std::fflush(stderr);
                // Catch's own SIGABRT handler would report from this thread, re-entering
                // this listener while the test case is still running on the main one.
                std::signal(SIGABRT, SIG_DFL);
                std::abort();
            }
            wake_.wait_for(lock, stop, std::chrono::seconds(1), [] { return false; });
        }
    }

    std::mutex                            mutex_;
    std::condition_variable_any           wake_;
    std::string                           name_;
    std::chrono::steady_clock::time_point started_;
    bool                                  running_ = false;
    std::jthread                          watcher_;
};

CATCH_REGISTER_LISTENER(TestWatchdog)

} // namespace
