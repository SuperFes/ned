// Pins the whole ned_tests binary to UTC before any TEST_CASE runs, so
// anything formatted in local time (Org clocks and "today", among others)
// reads the same on every machine. A test that needs a real offset uses
// Tests/EnvOverride.h's ScopedTimeZone.

#include <cstdlib>
#include <ctime>

namespace {

struct UtcForTests {
    UtcForTests() {
        ::setenv("TZ", "UTC", 1);
        ::tzset();
    }
};

const UtcForTests kUtcForTests;

} // namespace
