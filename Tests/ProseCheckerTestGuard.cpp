// prose-checking follow-up. Forces prose checking off for the entire
// ned_tests binary before any TEST_CASE runs, via a static-initialized
// guard object's constructor (Catch2WithMain's own main() only starts
// running test cases after every linked translation unit's static
// initializers have already run).
//
// Necessary because ProseChecker.h auto-wires harper-ls when it's found on
// $PATH -- and several existing test files (LspManagerTest.cpp,
// BufferViewTest.cpp, ModeLineTest.cpp) construct real LspManagers and call
// SyncBuffer, which now also attempts to sync the prose-checker connection
// independently of whatever they're actually testing. Without this guard,
// running ned_tests on any machine that has harper-ls installed -- the
// developer's own machine included -- would spawn a real harper-ls
// subprocess as a side effect of dozens of unrelated test cases, none of
// which pre-register a fake client for it the way FakeServer::Create does
// for their real language under test.
//
// Test cases that specifically want to exercise prose-checker behavior
// (Tests/ProseCheckerTest.cpp, the prose-specific cases in
// LspManagerTest.cpp) re-enable it locally and restore this disabled
// steady state before returning -- see RestoreProseCheckingDisabled in
// ProseCheckerTest.cpp.

#include "Editor/Lsp/ProseChecker.h"

namespace {

    struct DisableProseCheckingForTests {
        DisableProseCheckingForTests() {
            ned::editor::lsp::SetProseCheckingEnabled(false);
        }
    };

    const DisableProseCheckingForTests kDisableProseCheckingForTests;

} // namespace
