//
// AcpPanel/InlineImages -- decoding, fitting and drawing transcript pictures
// as cells through a real (headless-terminal) Notcurses context.
//

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "Editor/Acp/ContentBlocks.h"
#include "Text/Base64.h"
#include "UI/AcpPanel/InlineImages.h"
#include "UI/EventLoop.h"

using ned::editor::acp::Manager;
using ned::ui::Color;
using ned::ui::acppanel::InlineImages;

namespace {

// 3x2 RGBA: red, green, blue / white, clear, half-transparent dark blue.
constexpr const char* kPng = "iVBORw0KGgoAAAANSUhEUgAAAAMAAAACCAYAAACddGYaAAAAHUlEQVR4nAXBoQEAMAzAIHT1dG/t5xmIJEoVzNv7nJ4Ksxn7EooAAAAASUVORK5CYII=";

Manager::TranscriptImage Png() {
    return {.id = ned::editor::acp::NextImageId(), .mimeType = "image/png", .data = kPng};
}

} // namespace

TEST_CASE("InlineImages fits a picture without enlarging it, and refuses one that doesn't decode", "[AcpPanel][AcpImages]") {
    InlineImages images;
    // No terminal to ask: cells are taken as 10x20 pixels, so 3x2 pixels
    // is one cell.
    const auto fit = images.Fit(Png(), 40);
    REQUIRE(fit);
    REQUIRE(fit->columns == 1);
    REQUIRE(fit->rows == 1);

    REQUIRE_FALSE(images.Fit({.id = ned::editor::acp::NextImageId(), .data = "not base64!"}, 40));
    REQUIRE_FALSE(images.Fit({.id = ned::editor::acp::NextImageId(), .data = ned::text::Base64Encode("GIF89a")}, 40));
}

TEST_CASE("InlineImages draws a picture as cells through Notcurses, without an event loop drawing nothing", "[AcpPanel][AcpImages]") {
    const Manager::TranscriptImage image = Png();
    InlineImages                   headless;
    REQUIRE(headless.Cells(image, 3, 1, Color::RGB(0x000000)) == nullptr);
    REQUIRE_FALSE(headless.ShowPixels(image, {.x_min = 0, .x_max = 2, .y_min = 0, .y_max = 0}, Color::RGB(0x000000)));

    ned::ui::EventLoop eventLoop;
    InlineImages       images;
    images.SetEventLoop(&eventLoop);
    const std::vector<ned::ui::Cell>* cells = images.Cells(image, 3, 1, Color::RGB(0x000000));
    REQUIRE(cells != nullptr);
    REQUIRE(cells->size() == 3);
    // Each cell holds one column of the picture: red over white, then
    // green over nothing (the background), then blue over dark blue.
    std::uint8_t r = 0, g = 0, b = 0;
    bool         sawRed = false;
    for (const ned::ui::Cell& cell : *cells) {
        REQUIRE_FALSE(cell.character.empty());
        for (const Color& color : {cell.foreground_color, cell.background_color}) {
            ned::ui::ColorToRgb8(color, r, g, b);
            sawRed = sawRed || (r > 200 && g < 60 && b < 60);
        }
    }
    REQUIRE(sawRed);
    // The same request is served from the cache.
    REQUIRE(images.Cells(image, 3, 1, Color::RGB(0x000000)) == cells);
    images.EndFrame();
}
