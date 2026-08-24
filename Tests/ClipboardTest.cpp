#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

#include "Editor/Clipboard.h"

using ned::editor::BuildOsc52CopySequence;
using ned::editor::ClipboardEnabled;
using ned::editor::CopyToSystemClipboard;
using ned::editor::PasteFromSystemClipboard;
using ned::editor::ResolvedClipboardCopyCommand;
using ned::editor::ResolvedClipboardPasteCommand;
using ned::editor::SetClipboardCopyCommand;
using ned::editor::SetClipboardEnabled;
using ned::editor::SetClipboardPasteCommand;

namespace {

// ClipboardTestGuard.cpp forces ClipboardEnabled() false for the whole
// ned_tests binary (so a kill-ring/yank command test elsewhere never
// accidentally shells out to whatever clipboard tooling happens to be
// installed, or hangs waiting on a display server that doesn't exist in
// CI) -- every test here that flips it back on must restore that
// steady state before returning, even on an assertion failure. Mirrors
// Tests/ProseCheckerTest.cpp's RestoreProseCheckingDisabled exactly.
struct RestoreClipboardDisabled {
    ~RestoreClipboardDisabled() {
        SetClipboardCopyCommand({});
        SetClipboardPasteCommand({});
        SetClipboardEnabled(false);
    }
};

} // namespace

TEST_CASE("SetClipboardEnabled/ClipboardEnabled round-trip", "[Clipboard]") {
    const RestoreClipboardDisabled restore;

    SetClipboardEnabled(true);
    REQUIRE(ClipboardEnabled());

    SetClipboardEnabled(false);
    REQUIRE_FALSE(ClipboardEnabled());
}

TEST_CASE("SetClipboardCopyCommand/SetClipboardPasteCommand register independent overrides", "[Clipboard]") {
    const RestoreClipboardDisabled restore;

    SetClipboardCopyCommand({"my-copy-tool"});
    SetClipboardPasteCommand({"my-paste-tool", "-o"});

    REQUIRE(ResolvedClipboardCopyCommand() == std::vector<std::string>{"my-copy-tool"});
    REQUIRE(ResolvedClipboardPasteCommand() == std::vector<std::string>{"my-paste-tool", "-o"});
}

TEST_CASE("An empty argv clears a clipboard override rather than disabling resolution", "[Clipboard]") {
    const RestoreClipboardDisabled restore;

    SetClipboardCopyCommand({"a-made-up-clipboard-tool-nobody-has-installed"});
    REQUIRE(ResolvedClipboardCopyCommand() == std::vector<std::string>{"a-made-up-clipboard-tool-nobody-has-installed"});

    SetClipboardCopyCommand({}); // clears the override -- falls through to auto-detection, not to nullopt
    // Whatever auto-detection resolves to on the machine running this test,
    // it's never the cleared override -- the only assertion that's true
    // regardless of what's actually installed.
    REQUIRE(ResolvedClipboardCopyCommand() != std::vector<std::string>{"a-made-up-clipboard-tool-nobody-has-installed"});
}

TEST_CASE("BuildOsc52CopySequence base64-encodes text into an OSC 52 escape sequence", "[Clipboard]") {
    // "hi" -> base64 "aGk=" -- a small, hand-verifiable fixture rather than
    // reimplementing base64 in the test to check itself.
    REQUIRE(BuildOsc52CopySequence("hi", /*wrapForTmux=*/false) == "\x1b]52;c;aGk=\x07");
}

TEST_CASE("BuildOsc52CopySequence wraps the sequence in tmux's DCS passthrough envelope, doubling ESC", "[Clipboard]") {
    const std::string inner   = BuildOsc52CopySequence("hi", /*wrapForTmux=*/false);
    const std::string wrapped = BuildOsc52CopySequence("hi", /*wrapForTmux=*/true);

    REQUIRE(wrapped.starts_with("\x1bPtmux;"));
    REQUIRE(wrapped.ends_with("\x1b\\"));
    // The inner sequence's one literal ESC byte (its leading "\x1b]52;...")
    // must appear doubled inside the envelope, or tmux strips it instead of
    // forwarding it to the real terminal; the rest of the payload (base64
    // has no ESC bytes) is untouched.
    REQUIRE(inner.front() == '\x1b');
    REQUIRE(wrapped == std::string("\x1bPtmux;") + "\x1b\x1b]52;c;aGk=\x07" + "\x1b\\");
}

TEST_CASE("CopyToSystemClipboard/PasteFromSystemClipboard round-trip through a real subprocess", "[Clipboard]") {
    const RestoreClipboardDisabled restore;

    // A fake "clipboard" backed by a temp file, driven through sh/cat --
    // always present in CI, unlike xclip/wl-copy/pbcopy -- so this
    // genuinely exercises Clipboard.cpp's real ChildProcess-driving logic
    // (WriteAll/ReadSome/WaitForExit) rather than mocking it away.
    const std::filesystem::path fakeClipboard =
        std::filesystem::temp_directory_path() / "ned_clipboard_test_fake_clipboard";
    std::filesystem::remove(fakeClipboard);

    SetClipboardEnabled(true);
    SetClipboardCopyCommand({"sh", "-c", "cat > " + fakeClipboard.string()});
    SetClipboardPasteCommand({"sh", "-c", "cat " + fakeClipboard.string()});

    CopyToSystemClipboard("hello from ned");

    const std::optional<std::string> pasted = PasteFromSystemClipboard();
    REQUIRE(pasted.has_value());
    REQUIRE(*pasted == "hello from ned");

    std::filesystem::remove(fakeClipboard);
}

TEST_CASE("PasteFromSystemClipboard returns nullopt when disabled", "[Clipboard]") {
    const RestoreClipboardDisabled restore;

    SetClipboardEnabled(false);
    SetClipboardPasteCommand({"sh", "-c", "echo should-not-run"});

    REQUIRE_FALSE(PasteFromSystemClipboard().has_value());
}

TEST_CASE("PasteFromSystemClipboard returns nullopt when the resolved command exits non-zero", "[Clipboard]") {
    const RestoreClipboardDisabled restore;

    SetClipboardEnabled(true);
    SetClipboardPasteCommand({"sh", "-c", "exit 1"});

    REQUIRE_FALSE(PasteFromSystemClipboard().has_value());
}

TEST_CASE("PasteFromSystemClipboard kills and returns nullopt when the tool hangs past readTimeout", "[Clipboard]") {
    // subprocess-hang-protection follow-up.
    const RestoreClipboardDisabled restore;

    SetClipboardEnabled(true);
    SetClipboardPasteCommand({"sh", "-c", "sleep 100"});

    REQUIRE_FALSE(PasteFromSystemClipboard(std::chrono::milliseconds(100)).has_value());
}
