#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <optional>
#include <string>
#include <string_view>

#include "TestEvents.h"
#include "UI/ColorPicker.h"
#include "UI/Theme.h"
#include "UI/ThemePaints.h"
#include "UI/Widget.h"

using ned::editor::ColorLiteralOptions;
using ned::editor::ColorSyntax;
using ned::editor::ColorValue;
using ned::ui::Box;
using ned::ui::Canvas;
using ned::ui::ColorPicker;
using ned::ui::DarkTheme;
using ned::ui::Screen;
using ned::ui::Theme;

namespace {

using Channel = ColorPicker::Channel;

Screen PaintPicker(ColorPicker& picker, int width, int height) {
    Screen    screen(width, height);
    const Box box{.x_min = 0, .x_max = width - 1, .y_min = 0, .y_max = height - 1};
    picker.SetBox_(box);
    Canvas canvas(screen, box);
    picker.Paint(canvas);
    return screen;
}

std::string RowText(Screen& screen, int row, int width) {
    std::string text;
    for (int x = 0; x < width; ++x) {
        text += screen.PixelAt(x, row).character;
    }
    return text;
}

bool ScreenContains(Screen& screen, int width, int height, std::string_view needle) {
    for (int y = 0; y < height; ++y) {
        if (RowText(screen, y, width).find(needle) != std::string_view::npos) {
            return true;
        }
    }
    return false;
}

// Walks the selection down to `channel`, which is also the only way a test
// can drive one: the picker has no "set channel" entry point, deliberately.
void SelectChannel(ColorPicker& picker, Channel channel) {
    while (picker.SelectedChannel() != channel) {
        picker.OnEvent(ned::ui::test::ArrowDown());
    }
}

ColorPicker Opened(const Theme& theme, const ColorValue& colour, ColorSyntax syntax = ColorSyntax::Hex) {
    ColorPicker picker(theme);
    picker.Open(colour, syntax, ColorLiteralOptions{});
    picker.TakeFocus();
    return picker;
}

constexpr ColorValue kMagenta{.red = 1.0, .green = 0.0, .blue = 2.0 / 3.0, .alpha = 1.0};

struct AssumedBackgroundGuard {
    AssumedBackgroundGuard() : saved_(ned::ui::AssumedBackground()) {}
    ~AssumedBackgroundGuard() { ned::ui::SetAssumedBackground(saved_); }
    AssumedBackgroundGuard(const AssumedBackgroundGuard&)            = delete;
    AssumedBackgroundGuard& operator=(const AssumedBackgroundGuard&) = delete;

  private:
    std::optional<ned::ui::Color> saved_;
};

} // namespace

TEST_CASE("Opening reports the colour in both representations", "[ColorPicker]") {
    const Theme theme  = DarkTheme();
    ColorPicker picker = Opened(theme, kMagenta);

    REQUIRE(picker.ChannelValue(Channel::Red) == 255);
    REQUIRE(picker.ChannelValue(Channel::Green) == 0);
    REQUIRE(picker.ChannelValue(Channel::Blue) == 170);
    REQUIRE(picker.ChannelValue(Channel::Hue) == 320);
    REQUIRE(picker.ChannelValue(Channel::Saturation) == 100);
    REQUIRE(picker.ChannelValue(Channel::Lightness) == 50);
    REQUIRE(picker.ChannelValue(Channel::Alpha) == 100);
    REQUIRE(picker.Text() == "#ff00aa");
}

TEST_CASE("Editing an RGB channel re-derives HSL and vice versa", "[ColorPicker]") {
    const Theme theme  = DarkTheme();
    ColorPicker picker = Opened(theme, kMagenta);

    // Red down to zero: a blue colour now, so the hue must move with it.
    SelectChannel(picker, Channel::Red);
    picker.OnEvent(ned::ui::test::Home());
    REQUIRE(picker.ChannelValue(Channel::Red) == 0);
    REQUIRE(picker.ChannelValue(Channel::Hue) == 240);
    REQUIRE(picker.Text() == "#0000aa");

    // And the other direction: moving lightness rewrites all three bytes.
    SelectChannel(picker, Channel::Lightness);
    picker.OnEvent(ned::ui::test::Home());
    REQUIRE(picker.ChannelValue(Channel::Red) == 0);
    REQUIRE(picker.ChannelValue(Channel::Green) == 0);
    REQUIRE(picker.ChannelValue(Channel::Blue) == 0);
}

TEST_CASE("Hue survives a trip through black and grey", "[ColorPicker]") {
    const Theme theme  = DarkTheme();
    ColorPicker picker = Opened(theme, kMagenta);

    SelectChannel(picker, Channel::Lightness);
    picker.OnEvent(ned::ui::test::Home()); // black -- RgbToHsl can say nothing about hue
    REQUIRE(picker.ChannelValue(Channel::Hue) == 320);

    picker.OnEvent(ned::ui::test::End()); // white, the other achromatic end
    REQUIRE(picker.ChannelValue(Channel::Hue) == 320);

    // Desaturating all the way to grey costs the saturation -- which the
    // colour genuinely no longer has -- but not the hue, which nothing
    // about a grey can report.
    picker.OnEvent(ned::ui::test::Character('r'));
    SelectChannel(picker, Channel::Saturation);
    picker.OnEvent(ned::ui::test::Home());
    REQUIRE(picker.Text() == "#808080");
    REQUIRE(picker.ChannelValue(Channel::Hue) == 320);

    // ...so lifting it back up returns the colour it started from.
    picker.OnEvent(ned::ui::test::End());
    REQUIRE(picker.Text() == "#ff00aa");
}

TEST_CASE("A hue an intermediate colour really does have wins over the retained one", "[ColorPicker]") {
    const Theme theme  = DarkTheme();
    ColorPicker picker = Opened(theme, kMagenta);

    // Retention is a fallback for an unanswerable question, not a lock: the
    // moment an RGB edit lands on a colour that has a hue of its own, that
    // hue is the answer. Red alone out of black is red.
    SelectChannel(picker, Channel::Lightness);
    picker.OnEvent(ned::ui::test::Home());
    SelectChannel(picker, Channel::Red);
    picker.OnEvent(ned::ui::test::End());
    REQUIRE(picker.ChannelValue(Channel::Hue) == 0);
}

TEST_CASE("Hue wraps where every other channel clamps", "[ColorPicker]") {
    const Theme theme  = DarkTheme();
    ColorPicker picker = Opened(theme, kMagenta);

    SelectChannel(picker, Channel::Hue);
    picker.OnEvent(ned::ui::test::End()); // 359
    REQUIRE(picker.ChannelValue(Channel::Hue) == 359);
    picker.OnEvent(ned::ui::test::ArrowRight());
    REQUIRE(picker.ChannelValue(Channel::Hue) == 0);
    picker.OnEvent(ned::ui::test::ArrowLeft());
    REQUIRE(picker.ChannelValue(Channel::Hue) == 359);

    SelectChannel(picker, Channel::Red);
    picker.OnEvent(ned::ui::test::End());
    picker.OnEvent(ned::ui::test::ArrowRight());
    REQUIRE(picker.ChannelValue(Channel::Red) == 255);
    picker.OnEvent(ned::ui::test::Home());
    picker.OnEvent(ned::ui::test::ArrowLeft());
    REQUIRE(picker.ChannelValue(Channel::Red) == 0);
}

TEST_CASE("Shifted arrows step by ten", "[ColorPicker]") {
    const Theme theme  = DarkTheme();
    ColorPicker picker = Opened(theme, ColorValue{.red = 0.0, .green = 0.0, .blue = 0.0, .alpha = 1.0});

    SelectChannel(picker, Channel::Red);
    picker.OnEvent(ned::ui::test::ArrowRightShift());
    REQUIRE(picker.ChannelValue(Channel::Red) == 10);
    picker.OnEvent(ned::ui::test::ArrowLeftShift());
    REQUIRE(picker.ChannelValue(Channel::Red) == 0);
}

TEST_CASE("Selection wraps round the seven rows", "[ColorPicker]") {
    const Theme theme  = DarkTheme();
    ColorPicker picker = Opened(theme, kMagenta);

    REQUIRE(picker.SelectedChannel() == Channel::Red);
    picker.OnEvent(ned::ui::test::ArrowUp());
    REQUIRE(picker.SelectedChannel() == Channel::Alpha);
    picker.OnEvent(ned::ui::test::ArrowDown());
    REQUIRE(picker.SelectedChannel() == Channel::Red);
}

TEST_CASE("Tab cycles the notations this colour can be written in", "[ColorPicker]") {
    const Theme theme  = DarkTheme();
    ColorPicker picker = Opened(theme, kMagenta);

    REQUIRE(picker.Text() == "#ff00aa");
    picker.OnEvent(ned::ui::test::Tab());
    REQUIRE(picker.Text() != "#ff00aa");
    // Every offered spelling round-trips: ColorPresentations only ever
    // offers one that scans back to the colour it was offered for.
    for (int step = 0; step < 6; ++step) {
        REQUIRE_FALSE(picker.Text().empty());
        picker.OnEvent(ned::ui::test::Tab());
    }
}

TEST_CASE("A chosen notation survives an alpha change", "[ColorPicker]") {
    const Theme theme  = DarkTheme();
    ColorPicker picker = Opened(theme, kMagenta, ColorSyntax::Hsl);

    REQUIRE(picker.Text() == "hsl(320, 100%, 50%)");

    SelectChannel(picker, Channel::Alpha);
    picker.OnEvent(ned::ui::test::ArrowLeftShift()); // 90%
    REQUIRE(picker.ChannelValue(Channel::Alpha) == 90);
    REQUIRE(picker.Text().starts_with("hsla("));

    picker.OnEvent(ned::ui::test::ArrowRightShift()); // back to opaque
    REQUIRE(picker.Text() == "hsl(320, 100%, 50%)");
}

TEST_CASE("A language that admits no short hex is never offered one", "[ColorPicker]") {
    const Theme theme = DarkTheme();
    ColorPicker picker(theme);
    picker.Open(ColorValue{.red = 1.0, .green = 0.0, .blue = 0.0, .alpha = 1.0}, ColorSyntax::Hex,
                ColorLiteralOptions{.shortHex = false, .namedColors = false});
    picker.TakeFocus();

    for (int step = 0; step < 8; ++step) {
        REQUIRE(picker.Text() != "#f00");
        REQUIRE(picker.Text() != "red");
        picker.OnEvent(ned::ui::test::Tab());
    }

    ColorPicker permissive(theme);
    permissive.Open(ColorValue{.red = 1.0, .green = 0.0, .blue = 0.0, .alpha = 1.0}, ColorSyntax::HexShort,
                    ColorLiteralOptions{.shortHex = true, .namedColors = true});
    REQUIRE(permissive.Text() == "#f00");
}

TEST_CASE("r restores the colour the picker opened on", "[ColorPicker]") {
    const Theme theme  = DarkTheme();
    ColorPicker picker = Opened(theme, kMagenta);

    SelectChannel(picker, Channel::Green);
    picker.OnEvent(ned::ui::test::End());
    REQUIRE(picker.Text() != "#ff00aa");

    picker.OnEvent(ned::ui::test::Character('r'));
    REQUIRE(picker.Text() == "#ff00aa");
}

TEST_CASE("Enter hands back the text, Escape hands back nothing", "[ColorPicker]") {
    const Theme theme = DarkTheme();

    ColorPicker picker = Opened(theme, kMagenta);
    std::string accepted;
    bool        cancelled = false;
    picker.SetOnAccept([&accepted](std::string text) { accepted = std::move(text); });
    picker.SetOnCancel([&cancelled] { cancelled = true; });

    REQUIRE(picker.OnEvent(ned::ui::test::Return()));
    REQUIRE(accepted == "#ff00aa");
    REQUIRE_FALSE(cancelled);

    accepted.clear();
    REQUIRE(picker.OnEvent(ned::ui::test::Escape()));
    REQUIRE(cancelled);
    REQUIRE(accepted.empty());
}

TEST_CASE("An unfocused picker leaves keys alone, a focused one swallows them", "[ColorPicker]") {
    const Theme theme = DarkTheme();
    ColorPicker picker(theme);
    picker.Open(kMagenta, ColorSyntax::Hex, ColorLiteralOptions{});

    // Not focused: a keystroke belongs to whatever is underneath.
    REQUIRE_FALSE(picker.OnEvent(ned::ui::test::Character('x')));

    picker.TakeFocus();
    REQUIRE(picker.OnEvent(ned::ui::test::Character('x')));
    REQUIRE(picker.Text() == "#ff00aa"); // and changed nothing
}

TEST_CASE("Contrast is measured against the composited colour, not the opaque one", "[ColorPicker]") {
    Theme theme             = DarkTheme();
    theme.background        = ned::ui::Color::RGB(0, 0, 0);
    theme.defaultForeground = ned::ui::Color::RGB(255, 255, 255);

    ColorPicker picker = Opened(theme, ColorValue{.red = 1.0, .green = 1.0, .blue = 1.0, .alpha = 1.0});
    REQUIRE(picker.ContrastAgainstBackground().value_or(0.0) > 20.0); // white on black
    REQUIRE(picker.ContrastAgainstText().value_or(0.0) < 1.1);        // white on white

    // Half-transparent white over a black panel is mid-grey, which is what
    // a reader would actually see -- so both ratios move together.
    SelectChannel(picker, Channel::Alpha);
    for (int step = 0; step < 5; ++step) {
        picker.OnEvent(ned::ui::test::ArrowLeftShift());
    }
    REQUIRE(picker.ChannelValue(Channel::Alpha) == 50);
    REQUIRE(picker.ContrastAgainstBackground().value_or(0.0) < 10.0);
    REQUIRE(picker.ContrastAgainstText().value_or(0.0) > 1.1);
}

TEST_CASE("A colour with nothing behind it reports no ratio rather than a perfect one", "[ColorPicker]") {
    // ContrastRatio answers its own maximum for an unmeasurable pair, which
    // on a terminal-background theme would paint "21.0" -- a perfect score
    // where there is no score at all.
    // AssumedBackground is process-wide, and ChromeBackdrop consults it --
    // a detected backdrop left behind by another test file would make this
    // measurable again.
    const AssumedBackgroundGuard guard;
    ned::ui::SetAssumedBackground(std::nullopt);

    Theme theme             = DarkTheme();
    theme.background        = ned::ui::Color::Default;
    theme.defaultForeground = ned::ui::Color::Default;

    ColorPicker picker = Opened(theme, kMagenta);
    CHECK_FALSE(picker.ContrastAgainstBackground().has_value());
    CHECK_FALSE(picker.ContrastAgainstText().has_value());

    const int width  = 44;
    const int height = 16;
    Screen    screen = PaintPicker(picker, width, height);
    CHECK(ScreenContains(screen, width, height, "bg -"));
    CHECK_FALSE(ScreenContains(screen, width, height, "21.0"));
}

TEST_CASE("Painting shows the value, both preview halves and the channel letters", "[ColorPicker]") {
    const Theme theme  = DarkTheme();
    ColorPicker picker = Opened(theme, kMagenta);

    const int width  = 44;
    const int height = 16;
    Screen    screen = PaintPicker(picker, width, height);

    REQUIRE(ScreenContains(screen, width, height, "#ff00aa"));
    REQUIRE(ScreenContains(screen, width, height, "was"));
    REQUIRE(ScreenContains(screen, width, height, "now"));
    REQUIRE(ScreenContains(screen, width, height, "Colour"));
    REQUIRE(ScreenContains(screen, width, height, "255"));
    REQUIRE(ScreenContains(screen, width, height, "320"));

    // The "now" half is painted in the colour itself, which is the whole
    // point of the panel -- assert against a cell rather than a glyph.
    bool paintedTheColour = false;
    for (int y = 1; y < height - 1 && !paintedTheColour; ++y) {
        for (int x = 1; x < width - 1; ++x) {
            const ned::ui::Color& background = screen.PixelAt(x, y).background_color;
            if (background == ned::ui::Color::RGB(255, 0, 170)) {
                paintedTheColour = true;
                break;
            }
        }
    }
    REQUIRE(paintedTheColour);
}

TEST_CASE("A click on a slider track sets that channel", "[ColorPicker]") {
    const Theme theme  = DarkTheme();
    ColorPicker picker = Opened(theme, ColorValue{.red = 0.0, .green = 0.0, .blue = 0.0, .alpha = 1.0});

    const int width  = 44;
    const int height = 16;
    PaintPicker(picker, width, height); // establishes the box the click is resolved against

    // Row layout: border, three preview rows, a blank, then R/G/B/H/S/L/A.
    const int greenRow = 1 + 3 + 1 + 1;
    picker.OnEvent(ned::ui::test::Mouse(width - 1 - 5 - 1, greenRow, ned::ui::MouseEvent::Button::Left,
                                        ned::ui::MouseEvent::Motion::Pressed));
    REQUIRE(picker.SelectedChannel() == Channel::Green);
    REQUIRE(picker.ChannelValue(Channel::Green) == 255);

    picker.OnEvent(ned::ui::test::Mouse(1 + 4, greenRow, ned::ui::MouseEvent::Button::Left,
                                        ned::ui::MouseEvent::Motion::Pressed));
    REQUIRE(picker.ChannelValue(Channel::Green) == 0);
}

TEST_CASE("The picker degrades sanely for a canvas with no room", "[ColorPicker]") {
    const Theme theme  = DarkTheme();
    ColorPicker picker = Opened(theme, kMagenta);

    PaintPicker(picker, 2, 2); // must not crash
    PaintPicker(picker, 10, 5);
    PaintPicker(picker, 44, 6);
}
