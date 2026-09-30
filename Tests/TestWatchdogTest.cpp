#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>

#include <sys/wait.h>

#include "TestWatchdog.h"

using ned::test::WatchdogLimit;

TEST_CASE("WatchdogLimit reads NED_TEST_WATCHDOG_SECONDS", "[TestWatchdog]") {
    const auto defaultLimit = WatchdogLimit(nullptr);
    REQUIRE(defaultLimit.has_value());
    REQUIRE(WatchdogLimit("") == defaultLimit);
    REQUIRE(WatchdogLimit("soon") == defaultLimit);
    REQUIRE(WatchdogLimit("-5") == defaultLimit);
    REQUIRE(WatchdogLimit("45") == std::chrono::seconds(45));
    REQUIRE_FALSE(WatchdogLimit("0").has_value());
}

TEST_CASE("Watchdog probe: runs past a one-second limit", "[.][watchdog-probe]") {
    std::this_thread::sleep_for(std::chrono::seconds(30));
}

TEST_CASE("The watchdog aborts a test case that runs past its limit and names it", "[TestWatchdog]") {
    const std::filesystem::path self    = std::filesystem::read_symlink("/proc/self/exe");
    const std::string           command = "ulimit -c 0; NED_TEST_WATCHDOG_SECONDS=1 exec '" + self.string() + "' '[watchdog-probe]' 2>&1";

    FILE* child = ::popen(command.c_str(), "r");
    REQUIRE(child != nullptr);
    std::string           output;
    std::array<char, 512> chunk{};
    while (const std::size_t n = std::fread(chunk.data(), 1, chunk.size(), child)) {
        output.append(chunk.data(), n);
    }
    const int status = ::pclose(child);

    INFO(output);
    REQUIRE(WIFSIGNALED(status));
    REQUIRE(WTERMSIG(status) == SIGABRT);
    REQUIRE(output.find("ned_tests watchdog: \"Watchdog probe: runs past a one-second limit\"") != std::string::npos);
}
