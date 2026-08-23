//
// System-clipboard integration for KillRing/yank (ROADMAP.md's own "System
// clipboard integration" gap). Two independent mechanisms, not a strict
// primary/fallback -- CopyToSystemClipboard always attempts both:
//
//  - Shelling out to a small platform CLI tool (wl-copy/wl-paste on
//    Wayland, xclip -- falling back to xsel -- on X11, pbcopy/pbpaste on
//    macOS, clip.exe/powershell.exe under WSL), auto-detected once via
//    $PATH the same way ProseChecker.h auto-wires harper-ls, or an
//    explicit override. This is the only mechanism PasteFromSystemClipboard
//    can use at all.
//  - Writing a raw OSC 52 escape sequence directly to the terminal. OSC 52
//    has no acknowledgement, so "did it work" is unobservable -- it's
//    always attempted in addition to, never instead of, a resolved CLI
//    tool, because it's the only thing that reaches a *local* machine's
//    clipboard when ned is running over SSH with no clipboard tool
//    installed on the remote host (a CLI tool there, if one existed, would
//    only set the remote machine's own clipboard).
//
// OSC 52 read-back (letting the terminal answer with the clipboard's
// current contents) is a deliberate scope cut: unlike the OSC 10/11 color
// queries UI/TerminalColorProbe.h sends -- which run and finish strictly
// before ui::EventLoop's Notcurses context exists -- a paste needs to
// happen in the middle of a live editing session, where Notcurses already
// owns stdin's read loop. Racing an unsolicited OSC 52 reply against
// Notcurses' own input parser risks it being silently swallowed or
// misdecoded as garbage key events, with no cooperative hook exposed to
// tell the two apart safely. PasteFromSystemClipboard is CLI-tool-only.
//
// Windows: this codebase is POSIX-only throughout (posix_spawn, forkpty,
// termios) -- there is no native-Windows build to target at all today. The
// WSL detection below is real Linux userspace shelling out to
// clip.exe/powershell.exe over WSL's own interop, not a native port.
//

#ifndef NED_EDITOR_CLIPBOARD_H
#define NED_EDITOR_CLIPBOARD_H

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ned::editor {

// Default true. The hard kill switch -- both CopyToSystemClipboard and
// PasteFromSystemClipboard are a complete no-op (nullopt/nothing sent) when
// this is false, mirroring lsp::ProseCheckingEnabled's own shape. Also
// what keeps unit tests hermetic against whatever clipboard tooling
// happens to be installed and whatever terminal ned_tests happens to run
// under -- see Tests/ClipboardTestGuard.cpp, which forces this false for
// the whole ned_tests binary.
void               SetClipboardEnabled(bool enabled);
[[nodiscard]] bool ClipboardEnabled();

// Explicit overrides for the copy/paste CLI tool's argv, independently
// settable -- e.g. override just paste while leaving copy on
// auto-detection. Same "empty argv clears the override, reverts to
// auto-detection" convention as lsp::SetProseCheckerCommand/SetLspServerCommand.
void SetClipboardCopyCommand(std::vector<std::string> argv);
void SetClipboardPasteCommand(std::vector<std::string> argv);

// Resolution order, independently for copy and paste: an explicit override
// if set, else platform auto-detection (memoized after the first call,
// mirroring lsp::ProseChecker.h's own AutoDetect -- a $PATH/env scan
// shouldn't repeat on every kill/yank): $WAYLAND_DISPLAY set and
// wl-copy/wl-paste resolvable: those. Else $DISPLAY set and xclip
// resolvable: `xclip -selection clipboard -in`/`-out`. Else xsel
// resolvable: `xsel --clipboard --input`/`--output`. Else pbcopy/pbpaste
// resolvable (macOS): those. Else clip.exe and powershell.exe resolvable
// (WSL -- clip.exe for copy, `powershell.exe Get-Clipboard` for paste).
// Else std::nullopt.
[[nodiscard]] std::optional<std::vector<std::string>> ResolvedClipboardCopyCommand();
[[nodiscard]] std::optional<std::vector<std::string>> ResolvedClipboardPasteCommand();

// Copies text to the system clipboard: shells out to
// ResolvedClipboardCopyCommand() if one is resolved, and always also
// writes a raw OSC 52 escape sequence to the terminal (wrapped for tmux's
// own DCS passthrough convention if $TMUX is set) -- see this file's own
// header comment for why both, unconditionally. A complete no-op if
// ClipboardEnabled() is false.
void CopyToSystemClipboard(std::string_view text);

// Reads the system clipboard via ResolvedClipboardPasteCommand(), if one is
// resolved -- std::nullopt otherwise, including when ClipboardEnabled() is
// false or the resolved command exits non-zero. See this file's own header
// comment for why there is no OSC 52 fallback here.
[[nodiscard]] std::optional<std::string> PasteFromSystemClipboard();

// Exposed for testing (mirrors UI/TerminalColorProbe.h's own BuildColorQuery
// split): the pure OSC 52 escape-sequence construction, no I/O. wrapForTmux
// applies tmux's DCS passthrough envelope (doubling every literal ESC byte
// inside a `\033Ptmux;...\033\\` wrapper) -- tmux otherwise strips an OSC 52
// sequence rather than forwarding it to the real terminal underneath.
[[nodiscard]] std::string BuildOsc52CopySequence(std::string_view text, bool wrapForTmux);

} // namespace ned::editor

#endif // NED_EDITOR_CLIPBOARD_H
