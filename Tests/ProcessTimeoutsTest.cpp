#include <catch2/catch_test_macros.hpp>

#include <chrono>

#include "Editor/ProcessTimeouts.h"

using ned::editor::ProtocolReadStallTimeoutMs;
using ned::editor::ProtocolRequestTimeoutMs;
using ned::editor::ProtocolWriteStallTimeoutMs;
using ned::editor::SetProtocolReadStallTimeoutMs;
using ned::editor::SetProtocolRequestTimeoutMs;
using ned::editor::SetProtocolWriteStallTimeoutMs;
using ned::editor::SetSubprocessReadTimeoutMs;
using ned::editor::SetSubprocessWriteTimeoutMs;
using ned::editor::SubprocessReadTimeoutMs;
using ned::editor::SubprocessWriteTimeoutMs;

namespace {

// Process-wide state (see ProcessTimeouts.h's own doc comment); every test
// that sets one must restore its default for the next test, mirroring
// DiffRefreshSettingsTest.cpp's own guard shape exactly.
struct ProcessTimeoutsGuard {
    ~ProcessTimeoutsGuard() {
        SetSubprocessReadTimeoutMs(5000);
        SetSubprocessWriteTimeoutMs(5000);
        SetProtocolReadStallTimeoutMs(30000);
        SetProtocolWriteStallTimeoutMs(30000);
        SetProtocolRequestTimeoutMs(30000);
    }
};

} // namespace

TEST_CASE("Timeouts default to their documented values", "[ProcessTimeouts]") {
    const ProcessTimeoutsGuard guard;
    REQUIRE(SubprocessReadTimeoutMs() == std::chrono::milliseconds(5000));
    REQUIRE(SubprocessWriteTimeoutMs() == std::chrono::milliseconds(5000));
    REQUIRE(ProtocolReadStallTimeoutMs() == std::chrono::milliseconds(30000));
    REQUIRE(ProtocolWriteStallTimeoutMs() == std::chrono::milliseconds(30000));
    REQUIRE(ProtocolRequestTimeoutMs() == std::chrono::milliseconds(30000));
}

TEST_CASE("SetSubprocessReadTimeoutMs/SubprocessReadTimeoutMs round-trip", "[ProcessTimeouts]") {
    const ProcessTimeoutsGuard guard;
    SetSubprocessReadTimeoutMs(1500);
    REQUIRE(SubprocessReadTimeoutMs() == std::chrono::milliseconds(1500));
}

TEST_CASE("SetSubprocessWriteTimeoutMs/SubprocessWriteTimeoutMs round-trip", "[ProcessTimeouts]") {
    const ProcessTimeoutsGuard guard;
    SetSubprocessWriteTimeoutMs(1500);
    REQUIRE(SubprocessWriteTimeoutMs() == std::chrono::milliseconds(1500));
}

TEST_CASE("SetProtocolReadStallTimeoutMs/ProtocolReadStallTimeoutMs round-trip", "[ProcessTimeouts]") {
    const ProcessTimeoutsGuard guard;
    SetProtocolReadStallTimeoutMs(10000);
    REQUIRE(ProtocolReadStallTimeoutMs() == std::chrono::milliseconds(10000));
}

TEST_CASE("SetProtocolWriteStallTimeoutMs/ProtocolWriteStallTimeoutMs round-trip", "[ProcessTimeouts]") {
    const ProcessTimeoutsGuard guard;
    SetProtocolWriteStallTimeoutMs(15000);
    REQUIRE(ProtocolWriteStallTimeoutMs() == std::chrono::milliseconds(15000));
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

    SetSubprocessWriteTimeoutMs(0);
    REQUIRE(SubprocessWriteTimeoutMs() == std::chrono::milliseconds(1));
    SetSubprocessWriteTimeoutMs(-100);
    REQUIRE(SubprocessWriteTimeoutMs() == std::chrono::milliseconds(1));

    SetProtocolReadStallTimeoutMs(0);
    REQUIRE(ProtocolReadStallTimeoutMs() == std::chrono::milliseconds(1));
    SetProtocolReadStallTimeoutMs(-100);
    REQUIRE(ProtocolReadStallTimeoutMs() == std::chrono::milliseconds(1));

    SetProtocolWriteStallTimeoutMs(0);
    REQUIRE(ProtocolWriteStallTimeoutMs() == std::chrono::milliseconds(1));
    SetProtocolWriteStallTimeoutMs(-100);
    REQUIRE(ProtocolWriteStallTimeoutMs() == std::chrono::milliseconds(1));

    SetProtocolRequestTimeoutMs(0);
    REQUIRE(ProtocolRequestTimeoutMs() == std::chrono::milliseconds(1));
    SetProtocolRequestTimeoutMs(-100);
    REQUIRE(ProtocolRequestTimeoutMs() == std::chrono::milliseconds(1));
}
