#include <catch2/catch_test_macros.hpp>

#include <string>

#include "UI/EchoArea.h"
#include "UI/Theme.h"

namespace {

std::string RowText(ned::ui::Screen& screen, int row, int width) {
    std::string out;
    for (int col = 0; col < width; ++col) {
        out += screen.PixelAt(col, row).character;
    }
    return out;
}

} // namespace

TEST_CASE("EchoArea displays whatever the referenced message currently holds", "[EchoArea]") {
    std::string       message = "hello";
    ned::ui::Theme    theme   = ned::ui::DarkTheme();
    ned::ui::EchoArea echoArea(message, theme);

    ned::ui::Screen screen(20, 1);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 0});

    echoArea.Paint(canvas);
    REQUIRE(RowText(screen, 0, 5) == "hello");
    REQUIRE(screen.PixelAt(0, 0).foreground_color == theme.echoArea.foreground);
    REQUIRE(screen.PixelAt(0, 0).background_color == theme.echoArea.background);

    message = "updated";
    echoArea.Paint(canvas); // Paint reads message_ fresh each call, no external sync needed
    REQUIRE(RowText(screen, 0, 7) == "updated");
}

// fuzzy-candidate-list-styling follow-up.

TEST_CASE("EmphasizeForEchoArea/DimForEchoArea wrap text in invisible sentinel bytes, not visible markup",
          "[EchoArea]") {
    const std::string emphasized = ned::ui::EmphasizeForEchoArea("foo");
    REQUIRE(emphasized.find("foo") != std::string::npos);
    REQUIRE(emphasized.size() == 5); // "foo" plus one start and one end sentinel byte, both invisible

    const std::string dimmed = ned::ui::DimForEchoArea("bar");
    REQUIRE(dimmed.find("bar") != std::string::npos);
    REQUIRE(dimmed.size() == 5);

    const std::string ghosted = ned::ui::GhostForEchoArea("baz");
    REQUIRE(ghosted.find("baz") != std::string::npos);
    REQUIRE(ghosted.size() == 5);
}

TEST_CASE("EchoArea::Paint renders an emphasized span bold and strips its sentinel bytes", "[EchoArea]") {
    const std::string message = "before " + ned::ui::EmphasizeForEchoArea("BOLD") + " after";
    ned::ui::Theme    theme   = ned::ui::DarkTheme();
    ned::ui::EchoArea echoArea(message, theme);

    ned::ui::Screen screen(30, 1);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 29, .y_min = 0, .y_max = 0});
    echoArea.Paint(canvas);

    // The sentinel bytes are consumed as zero-width markup, not rendered as
    // their own columns -- "before BOLD after" reads contiguously, with no
    // gap where the invisible markers were.
    REQUIRE(RowText(screen, 0, 17) == "before BOLD after");
    REQUIRE_FALSE(screen.PixelAt(0, 0).bold);  // 'b' of "before" -- unaffected
    REQUIRE(screen.PixelAt(7, 0).bold);        // 'B' of "BOLD"
    REQUIRE(screen.PixelAt(10, 0).bold);       // 'D' of "BOLD"
    REQUIRE_FALSE(screen.PixelAt(12, 0).bold); // 'a' of "after" -- back to normal
}

TEST_CASE("EchoArea::Paint renders a dimmed span with a blended foreground and strips its sentinel bytes",
          "[EchoArea]") {
    const std::string message = "before " + ned::ui::DimForEchoArea("DIM") + " after";
    ned::ui::Theme    theme   = ned::ui::DarkTheme();
    ned::ui::EchoArea echoArea(message, theme);

    ned::ui::Screen screen(30, 1);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 29, .y_min = 0, .y_max = 0});
    echoArea.Paint(canvas);

    REQUIRE(RowText(screen, 0, 16) == "before DIM after");
    REQUIRE(screen.PixelAt(0, 0).foreground_color == theme.echoArea.foreground);  // 'b' -- untouched
    REQUIRE(screen.PixelAt(7, 0).foreground_color != theme.echoArea.foreground);  // 'D' of "DIM" -- blended
    REQUIRE(screen.PixelAt(11, 0).foreground_color == theme.echoArea.foreground); // 'a' of "after" -- back to normal
}

TEST_CASE("EchoArea::Paint renders a ghosted span italic, more faded than a dimmed span, and strips its sentinel bytes",
          "[EchoArea]") {
    const std::string message = "before " + ned::ui::DimForEchoArea("DIM") + " " + ned::ui::GhostForEchoArea("GHOST") +
                                 " after";
    ned::ui::Theme    theme = ned::ui::DarkTheme();
    ned::ui::EchoArea echoArea(message, theme);

    ned::ui::Screen screen(40, 1);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 0});
    echoArea.Paint(canvas);

    REQUIRE(RowText(screen, 0, 22) == "before DIM GHOST after");
    REQUIRE_FALSE(screen.PixelAt(7, 0).italic);                                    // 'D' of "DIM" -- not italic
    REQUIRE(screen.PixelAt(11, 0).italic);                                         // 'G' of "GHOST" -- italic
    REQUIRE(screen.PixelAt(11, 0).foreground_color != theme.echoArea.foreground);  // blended
    REQUIRE(screen.PixelAt(11, 0).foreground_color != screen.PixelAt(7, 0).foreground_color); // more faded than DIM
    REQUIRE_FALSE(screen.PixelAt(17, 0).italic); // 'a' of "after" -- back to normal
}

// UTF-8-aware-rendering follow-up: a multi-byte codepoint must occupy
// exactly one cell/column, not one cell per byte -- found live via
// lsp-signature-help's active-parameter marker rendering as blank padding
// (see ROADMAP.md) before EchoArea::Paint was made codepoint-aware.
TEST_CASE("EchoArea::Paint renders a multi-byte UTF-8 character as one cell, not one per byte", "[EchoArea]") {
    const std::string message = "caf\xc3\xa9!"; // "café!" -- 'é' is a 2-byte UTF-8 codepoint
    ned::ui::Theme    theme   = ned::ui::DarkTheme();
    ned::ui::EchoArea echoArea(message, theme);

    ned::ui::Screen screen(20, 1);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 0});
    echoArea.Paint(canvas);

    REQUIRE(screen.PixelAt(0, 0).character == "c");
    REQUIRE(screen.PixelAt(1, 0).character == "a");
    REQUIRE(screen.PixelAt(2, 0).character == "f");
    REQUIRE(screen.PixelAt(3, 0).character == "\xc3\xa9"); // the whole 'é' in one cell
    REQUIRE(screen.PixelAt(4, 0).character == "!");        // not pushed one column right by a split 'é'
}

TEST_CASE("EchoArea::Paint emphasizes a multi-byte span without corrupting it", "[EchoArea]") {
    const std::string message = "before " + ned::ui::EmphasizeForEchoArea("caf\xc3\xa9") + " after";
    ned::ui::Theme    theme   = ned::ui::DarkTheme();
    ned::ui::EchoArea echoArea(message, theme);

    ned::ui::Screen screen(30, 1);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 29, .y_min = 0, .y_max = 0});
    echoArea.Paint(canvas);

    REQUIRE(screen.PixelAt(7, 0).character == "c");
    REQUIRE(screen.PixelAt(10, 0).character == "\xc3\xa9"); // 'é' of "café", still one cell
    REQUIRE(screen.PixelAt(10, 0).bold);
    REQUIRE(screen.PixelAt(11, 0).character == " ");
    REQUIRE_FALSE(screen.PixelAt(11, 0).bold);
}
