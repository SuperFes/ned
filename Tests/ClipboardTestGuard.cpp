// system-clipboard follow-up. Forces clipboard integration off for the
// entire ned_tests binary before any TEST_CASE runs, via a static-
// initialized guard object's constructor (Catch2WithMain's own main() only
// starts running test cases after every linked translation unit's static
// initializers have already run) -- mirrors Tests/ProseCheckerTestGuard.cpp
// exactly, for the same reason: Editor/Clipboard.h auto-detects and shells
// out to whatever xclip/wl-copy/xsel/pbcopy/clip.exe happens to be on
// $PATH, and some of those (xclip -o in particular, waiting on an X11
// selection owner that never answers under a headless CI display) can hang
// rather than fail fast. Kill-ring/yank command tests exercise
// Clipboard.h's call sites as a side effect regardless -- this guard keeps
// that side effect a safe no-op everywhere except Tests/ClipboardTest.cpp
// itself, which re-enables it locally with an injected fake backend and
// restores this disabled steady state before returning.

#include "Editor/Clipboard.h"

namespace {

    struct DisableClipboardForTests {
        DisableClipboardForTests() {
            ned::editor::SetClipboardEnabled(false);
        }
    };

    const DisableClipboardForTests kDisableClipboardForTests;

} // namespace
