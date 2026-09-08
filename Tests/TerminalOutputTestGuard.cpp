// headless-test-output follow-up. Forces EventLoop's Notcurses context to
// render into /dev/null for the entire ned_tests binary before any TEST_CASE
// runs, via a static-initialized guard object's constructor -- mirrors
// Tests/ClipboardTestGuard.cpp exactly (Catch2WithMain's own main() only
// starts running test cases after every linked translation unit's static
// initializers have already run), for the same class of reason: ned_tests
// constructs a real EventLoop per test case, and every one of those wrote
// alt-screen/cursor/bracketed-paste escape sequences straight to whatever
// terminal the suite happened to be running under, and mutated that
// terminal's own stdin termios flags. See ned::ui::SetHeadlessOutputForTesting's
// own doc comment. Nothing turns this back off.

#include "UI/EventLoop.h"

namespace {

struct HeadlessTerminalOutputForTests {
    HeadlessTerminalOutputForTests() {
        ned::ui::SetHeadlessOutputForTesting(true);
    }
};

const HeadlessTerminalOutputForTests kHeadlessTerminalOutputForTests;

} // namespace
