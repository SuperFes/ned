// async-write-queue follow-up. main.cpp ignores SIGPIPE process-wide (see
// its own comment) so a write to an already-closed pipe surfaces as an
// ordinary EPIPE/std::runtime_error instead of killing the process --
// Catch2WithMain's generated main() never runs that code, so ned_tests has
// no such protection on its own. LspClient's async write queue (writeThread_)
// widened the window in which a still-queued write can land after a test's
// own pipe fds are closed, and a real SIGPIPE-killed test run is exactly
// what surfaced this gap. Mirrors Tests/ClipboardTestGuard.cpp/
// Tests/ProseCheckerTestGuard.cpp's own static-initialized-guard pattern.

#include <csignal>

namespace {

    struct IgnoreSigpipeForTests {
        IgnoreSigpipeForTests() {
            std::signal(SIGPIPE, SIG_IGN);
        }
    };

    const IgnoreSigpipeForTests kIgnoreSigpipeForTests;

} // namespace
