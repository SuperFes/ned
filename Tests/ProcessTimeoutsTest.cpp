#include <catch2/catch_test_macros.hpp>

#include <chrono>

#include "Editor/ProcessTimeouts.h"

using ned::editor::ProtocolRequestTimeoutMs;
using ned::editor::ProtocolStallTimeoutMs;
using ned::editor::SetProtocolRequestTimeoutMs;
using ned::editor::SetProtocolStallTimeoutMs;
using ned::editor::SetSubprocessReadTimeoutMs;
using ned::editor::SubprocessReadTimeoutMs;

namespace {

// Process-wide state (see ProcessTimeouts.h's own doc comment); every test
// that sets one must restore its default for the next test, mirroring
// DiffRefreshSettingsTest.cpp's own guard shape exactly.
struct ProcessTimeoutsGuard {
    ~ProcessTimeoutsGuard() {
        SetSubprocessReadTimeoutMs(5000);
        SetProtocolStallTimeoutMs(30000);
        SetProtocolRequestTimeoutMs(30000);
    }
};

} // namespace

TEST_CASE("Timeouts default to their documented values", "[ProcessTimeouts]") {
    const ProcessTimeoutsGuard guard;
    REQUIRE(SubprocessReadTimeoutMs() == std::chrono::milliseconds(5000));
    REQUIRE(ProtocolStallTimeoutMs() == std::chrono::milliseconds(30000));
    REQUIRE(ProtocolRequestTimeoutMs() == std::chrono::milliseconds(30000));
}

TEST_CASE("SetSubprocessReadTimeoutMs/SubprocessReadTimeoutMs round-trip", "[ProcessTimeouts]") {
    const ProcessTimeoutsGuard guard;
    SetSubprocessReadTimeoutMs(1500);
    REQUIRE(SubprocessReadTimeoutMs() == std::chrono::milliseconds(1500));
}

TEST_CASE("SetProtocolStallTimeoutMs/ProtocolStallTimeoutMs round-trip", "[ProcessTimeouts]") {
    const ProcessTimeoutsGuard guard;
    SetProtocolStallTimeoutMs(10000);
    REQUIRE(ProtocolStallTimeoutMs() == std::chrono::milliseconds(10000));
}

TEST_CASE("SetProtocolRequestTimeoutMs/ProtocolRequestTimeoutMs round-trip", "[ProcessTimeouts]") {
    const ProcessTimeoutsGuard guard;
    SetProtocolRequestTimeoutMs(45000);
    REQUIRE(ProtocolRequestTimeoutMs() == std::chrono::milliseconds(45000));
}

TEST_CASE("Every setter clamps a non-positive value to 1ms", "[ProcessTimeouts]") {
    const ProcessTimeoutsGuard guard;

    SetSubprocessReadTimeoutMs(0);
    REQUIRE(SubprocessReadTimeoutMs() == std::chrono::milliseconds(1));
    SetSubprocessReadTimeoutMs(-100);
    REQUIRE(SubprocessReadTimeoutMs() == std::chrono::milliseconds(1));

    SetProtocolStallTimeoutMs(0);
    REQUIRE(ProtocolStallTimeoutMs() == std::chrono::milliseconds(1));
    SetProtocolStallTimeoutMs(-100);
    REQUIRE(ProtocolStallTimeoutMs() == std::chrono::milliseconds(1));

    SetProtocolRequestTimeoutMs(0);
    REQUIRE(ProtocolRequestTimeoutMs() == std::chrono::milliseconds(1));
    SetProtocolRequestTimeoutMs(-100);
    REQUIRE(ProtocolRequestTimeoutMs() == std::chrono::milliseconds(1));
}
