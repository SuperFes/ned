#include <catch2/catch_test_macros.hpp>

#include <string>

#include "UI/TerminalColorProbe.h"

using ned::ui::BuildColorQuery;
using ned::ui::BuildDetectedTheme;
using ned::ui::Color;
using ned::ui::DetectedColors;
using ned::ui::ParseColorReplies;

TEST_CASE("BuildColorQuery asks for OSC 10, OSC 11, and all 16 OSC 4 palette slots", "[TerminalColorProbe]") {
    const std::string query = BuildColorQuery();

    REQUIRE(query.find("\x1b]10;?\x07") != std::string::npos);
    REQUIRE(query.find("\x1b]11;?\x07") != std::string::npos);
    for (int i = 0; i < 16; ++i) {
        REQUIRE(query.find("\x1b]4;" + std::to_string(i) + ";?\x07") != std::string::npos);
    }
}

TEST_CASE("ParseColorReplies extracts a BEL-terminated OSC 11 reply", "[TerminalColorProbe]") {
    const DetectedColors detected = ParseColorReplies("\x1b]11;rgb:1e1e/2020/2424\x07");

    REQUIRE(detected.background.has_value());
    REQUIRE(detected.background->red == 0x1e);
    REQUIRE(detected.background->green == 0x20);
    REQUIRE(detected.background->blue == 0x24);
}

TEST_CASE("ParseColorReplies extracts an ST-terminated OSC 10 reply", "[TerminalColorProbe]") {
    const DetectedColors detected = ParseColorReplies("\x1b]10;rgb:d0d0/d0d0/d0d0\x1b\\");

    REQUIRE(detected.foreground.has_value());
    REQUIRE(detected.foreground->red == 0xd0);
}

TEST_CASE("ParseColorReplies truncates wider-than-a-byte channel groups to the first two hex digits", "[TerminalColorProbe]") {
    // Some terminals reply with 2 hex digits per channel instead of 4.
    const DetectedColors detected = ParseColorReplies("\x1b]11;rgb:1e/20/24\x07");

    REQUIRE(detected.background.has_value());
    REQUIRE(detected.background->red == 0x1e);
    REQUIRE(detected.background->green == 0x20);
    REQUIRE(detected.background->blue == 0x24);
}

TEST_CASE("ParseColorReplies disambiguates single- vs double-digit OSC 4 palette indices", "[TerminalColorProbe]") {
    const DetectedColors detected = ParseColorReplies(
        "\x1b]4;1;rgb:1100/0000/0000\x07"
        "\x1b]4;10;rgb:aa00/bb00/cc00\x07");

    REQUIRE(detected.palette[1].has_value());
    REQUIRE(detected.palette[1]->red == 0x11);
    REQUIRE(detected.palette[10].has_value());
    REQUIRE(detected.palette[10]->red == 0xaa);
}

TEST_CASE("ParseColorReplies leaves everything unset for an empty or garbage buffer", "[TerminalColorProbe]") {
    const DetectedColors detected = ParseColorReplies("not an OSC reply at all");

    REQUIRE_FALSE(detected.foreground.has_value());
    REQUIRE_FALSE(detected.background.has_value());
    for (const auto& entry : detected.palette) {
        REQUIRE_FALSE(entry.has_value());
    }
}

TEST_CASE("BuildDetectedTheme maps palette slots onto the same fields DarkTheme references symbolically", "[TerminalColorProbe]") {
    DetectedColors detected;
    detected.background = Color::RGB(0x10, 0x10, 0x10);
    detected.foreground = Color::RGB(0xe0, 0xe0, 0xe0);
    detected.palette[2] = Color::RGB(0x00, 0xff, 0x00); // green -> string
    detected.palette[4] = Color::RGB(0x00, 0x00, 0xff); // blue -> keyword/selection

    const auto theme = BuildDetectedTheme(detected, ned::ui::DarkTheme());

    REQUIRE(theme.background == *detected.background);
    REQUIRE(theme.defaultForeground == *detected.foreground);
    REQUIRE(theme.stringForeground == *detected.palette[2]);
    REQUIRE(theme.keywordForeground == *detected.palette[4]);
    REQUIRE(theme.selectionBackground == *detected.palette[4]);
}

TEST_CASE("BuildDetectedTheme keeps the fallback's value for anything not detected", "[TerminalColorProbe]") {
    const DetectedColors detected; // nothing set
    const auto           fallback = ned::ui::LightTheme();
    const auto           theme    = BuildDetectedTheme(detected, fallback);

    REQUIRE(theme.background == fallback.background);
    REQUIRE(theme.stringForeground == fallback.stringForeground);
    REQUIRE(theme.modeLineGradientStart == fallback.modeLineGradientStart);
}

TEST_CASE("BuildDetectedTheme derives the mode-line gradient from the detected background", "[TerminalColorProbe]") {
    DetectedColors detected;
    detected.background = Color::RGB(0x80, 0x80, 0x80);

    const auto theme = BuildDetectedTheme(detected, ned::ui::DarkTheme());

    // Lighter tint and darker tint of the detected background, not the fallback's fixed endpoints.
    REQUIRE(theme.modeLineGradientStart.red > 0x80);
    REQUIRE(theme.modeLineGradientEnd.red < 0x80);
}

TEST_CASE("BuildDetectedTheme derives border chrome from the detected colors", "[TerminalColorProbe]") {
    DetectedColors detected;
    detected.background = Color::RGB(0x10, 0x10, 0x10);
    detected.palette[5] = Color::RGB(0xc0, 0x40, 0xc0); // magenta -> border accent

    const auto fallback = ned::ui::DarkTheme();
    const auto theme    = BuildDetectedTheme(detected, fallback);

    // Border line: a stronger tint of the detected background, not the fallback literal.
    REQUIRE(theme.border.foreground == Color::RGB(0x10 + 45, 0x10 + 45, 0x10 + 45));
    REQUIRE(theme.borderAccent.foreground == *detected.palette[5]);
    // Focused gradient: pulled from the derived gradient toward the accent, so it
    // must differ from both the fallback's literals and the plain derived gradient.
    REQUIRE(theme.modeLineFocusedGradientStart != fallback.modeLineFocusedGradientStart);
    REQUIRE(theme.modeLineFocusedGradientStart != theme.modeLineGradientStart);
}

TEST_CASE("BuildDetectedTheme keeps fallback border chrome when nothing was detected", "[TerminalColorProbe]") {
    const DetectedColors detected;
    const auto           fallback = ned::ui::DarkTheme();
    const auto           theme    = BuildDetectedTheme(detected, fallback);

    REQUIRE(theme.border == fallback.border);
    REQUIRE(theme.borderAccent == fallback.borderAccent);
    REQUIRE(theme.modeLineFocusedGradientStart == fallback.modeLineFocusedGradientStart);
    REQUIRE(theme.modeLineFocusedGradientEnd == fallback.modeLineFocusedGradientEnd);
}
