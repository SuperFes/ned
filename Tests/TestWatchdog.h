#ifndef NED_TESTS_TESTWATCHDOG_H
#define NED_TESTS_TESTWATCHDOG_H

#include <chrono>
#include <optional>

namespace ned::test {

// The per-test-case limit NED_TEST_WATCHDOG_SECONDS asks for: nullopt when it
// turns the watchdog off, the default when it is unset or not a number.
std::optional<std::chrono::seconds> WatchdogLimit(const char* setting);

} // namespace ned::test

#endif // NED_TESTS_TESTWATCHDOG_H
