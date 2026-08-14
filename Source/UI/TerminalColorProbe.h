//
// Detects a terminal's actual configured colors (foreground, background, and
// the 16-slot ANSI palette) via OSC 10/11/4 queries, for the `--detect-theme`
// CLI mode (see main.cpp) -- not run on every launch, since it requires
// putting stdin into raw mode and racing a bounded timeout against a
// terminal that may not reply at all. FTXUI has no support for this: its
// terminal input parser doesn't parse OSC replies (it will mangle one into a
// stream of garbage key events), and its own App/ScreenInteractive starts
// reading stdin the moment its event loop runs, so this must run and finish
// *before* that starts -- the same "before the TUI library's own terminal
// setup" constraint this file has always documented, just against a
// different library now.
//
// Split in two deliberately: BuildColorQuery/ParseColorReplies are pure and
// unit-testable; ProbeTerminalColors is the raw termios/poll/read half that
// only a real terminal can meaningfully exercise (same category as the rest
// of this project's terminal-I/O code -- see BufferView's manual smoke-test
// precedent).
//

#ifndef NED_UI_TERMINALCOLORPROBE_H
#define NED_UI_TERMINALCOLORPROBE_H

#include <array>
#include <chrono>
#include <optional>
#include <string>
#include <string_view>

#include "Theme.h"

namespace ned::ui {

struct DetectedColors {
    std::optional<Color>                foreground; // OSC 10
    std::optional<Color>                background; // OSC 11
    std::array<std::optional<Color>, 16> palette;    // OSC 4;0 .. OSC 4;15
};

// The raw OSC 10/11/4;0-15 query string to write to the terminal, all
// batched into one write so an unresponsive terminal costs one timeout
// period total rather than one per query.
[[nodiscard]] std::string BuildColorQuery();

// Parses whatever raw bytes came back after writing BuildColorQuery() (a mix
// of OSC replies, possibly interleaved with unrelated bytes if the terminal
// echoed the query or sent something else) into whatever color replies it
// contains. Missing/malformed replies are simply absent from the result, not
// an error.
[[nodiscard]] DetectedColors ParseColorReplies(std::string_view buffer);

// Puts stdin into raw/non-canonical mode, writes BuildColorQuery(), reads
// with a bounded total timeout, and restores the original terminal mode via
// RAII regardless of how this returns (including on exception) -- a
// terminal must never be left in raw mode. POSIX-only. Must be called before
// FTXUI's own event loop starts reading stdin.
[[nodiscard]] DetectedColors ProbeTerminalColors(std::chrono::milliseconds timeout = std::chrono::milliseconds{300});

// Maps DetectedColors onto a Theme's fields, following the same semantic
// slots DarkTheme() already uses symbolically (comment -> bright black,
// string -> green, keyword -> blue, number -> magenta, ...). Any field
// whose corresponding OSC reply never arrived keeps its value from
// `fallback`. The mode-line gradient and selection/isearch-match overlay
// colors -- which have no direct ANSI-palette slot -- are derived as tints
// of the detected background rather than left at fallback's fixed values,
// so a detected theme still looks coherent rather than half-detected.
[[nodiscard]] Theme BuildDetectedTheme(const DetectedColors& detected, const Theme& fallback);

} // namespace ned::ui

#endif // NED_UI_TERMINALCOLORPROBE_H
