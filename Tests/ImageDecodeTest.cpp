#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <string>
#include <vector>

#include "Editor/Image/Decode.h"
#include "Text/Base64.h"

using namespace ned::editor::image;

namespace {

// 3x2 RGBA: red, green, blue / white, clear, half-transparent dark blue.
constexpr const char* kPng  = "iVBORw0KGgoAAAANSUhEUgAAAAMAAAACCAYAAACddGYaAAAAHUlEQVR4nAXBoQEAMAzAIHT1dG/t5xmIJEoVzNv7nJ4Ksxn7EooAAAAASUVORK5CYII=";
constexpr const char* kWebp = "UklGRj4AAABXRUJQVlA4TDIAAAAvAkAAEC8gECCI8J9qQ0iQ0P2/V4EAQYn/SgKCouuWCwA/ObYqKEjbgMXdU7KI/sfVAQ==";
// 8x8, solid (200, 40, 40).
constexpr const char* kJpeg =
    "/9j/4AAQSkZJRgABAQAAAQABAAD/2wBDAAIBAQEBAQIBAQECAgICAgQDAgICAgUEBAMEBgUGBgYFBgYGBwkIBgcJBwYGCAsICQoKCgoKBggLDAsKDAkKCgr/"
    "2wBDAQICAgICAgUDAwUKBwYHCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgr/wAARCAAIAAgDASIAAhEBAxEB/"
    "8QAHwAAAQUBAQEBAQEAAAAAAAAAAAECAwQFBgcICQoL/8QAtRAAAgEDAwIEAwUFBAQAAAF9AQIDAAQRBRIhMUEGE1FhByJxFDKBkaEII0KxwRVS0fAkM2Jyggk"
    "KFhcYGRolJicoKSo0NTY3ODk6Q0RFRkdISUpTVFVWV1hZWmNkZWZnaGlqc3R1dnd4eXqDhIWGh4iJipKTlJWWl5iZmqKjpKWmp6ipqrKztLW2t7i5usLDxMXGx8j"
    "JytLT1NXW19jZ2uHi4+Tl5ufo6erx8vP09fb3+Pn6/8QAHwEAAwEBAQEBAQEBAQAAAAAAAAECAwQFBgcICQoL/8QAtREAAgECBAQDBAcFBAQAAQJ3AAECAxEEBSE"
    "xBhJBUQdhcRMiMoEIFEKRobHBCSMzUvAVYnLRChYkNOEl8RcYGRomJygpKjU2Nzg5OkNERUZHSElKU1RVVldYWVpjZGVmZ2hpanN0dXZ3eHl6goOEhYaHiImKkp"
    "OUlZaXmJmaoqOkpaanqKmqsrO0tba3uLm6wsPExcbHyMnK0tPU1dbX2Nna4uPk5ebn6Onq8vP09fb3+Pn6/9oADAMBAAIRAxEAPwD5fooor8TP9QD/2Q==";

std::string Bytes(const char* base64) {
    return ned::text::Base64Decode(base64).value();
}

std::vector<int> Pixel(const RgbaImage& image, int x, int y) {
    const std::size_t i = (static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) + static_cast<std::size_t>(x)) * 4;
    return {image.pixels[i], image.pixels[i + 1], image.pixels[i + 2], image.pixels[i + 3]};
}

} // namespace

TEST_CASE("Base64Decode reverses Base64Encode, skipping whitespace", "[Image]") {
    using ned::text::Base64Decode;
    using ned::text::Base64Encode;
    for (const std::string& sample : std::vector<std::string>{"", "a", "ab", "abc", std::string("\0\xff\x10", 3)}) {
        REQUIRE(Base64Decode(Base64Encode(sample)) == sample);
    }
    REQUIRE(Base64Decode("YW\nJj") == "abc");
    REQUIRE_FALSE(Base64Decode("ab$c"));
    REQUIRE_FALSE(Base64Decode("YQ==YQ=="));
    REQUIRE_FALSE(Base64Decode("Y"));
}

TEST_CASE("DecodeImage reads PNG and lossless WebP pixel for pixel", "[Image]") {
    for (const char* fixture : {kPng, kWebp}) {
        const std::optional<RgbaImage> image = DecodeImage(Bytes(fixture));
        REQUIRE(image);
        REQUIRE(image->width == 3);
        REQUIRE(image->height == 2);
        REQUIRE(Pixel(*image, 0, 0) == std::vector<int>{255, 0, 0, 255});
        REQUIRE(Pixel(*image, 2, 0) == std::vector<int>{0, 0, 255, 255});
        REQUIRE(Pixel(*image, 1, 1)[3] == 0);
        REQUIRE(Pixel(*image, 2, 1) == std::vector<int>{10, 20, 30, 128});
    }
}

TEST_CASE("DecodeImage reads a JPEG as opaque RGBA", "[Image]") {
    const std::optional<RgbaImage> image = DecodeImage(Bytes(kJpeg));
    REQUIRE(image);
    REQUIRE(image->width == 8);
    REQUIRE(image->height == 8);
    const std::vector<int> pixel = Pixel(*image, 4, 4);
    REQUIRE(std::abs(pixel[0] - 200) < 6);
    REQUIRE(std::abs(pixel[1] - 40) < 6);
    REQUIRE(pixel[3] == 255);
}

TEST_CASE("DecodeImage refuses unknown formats and damaged images", "[Image]") {
    REQUIRE_FALSE(DecodeImage("GIF89a......"));
    REQUIRE_FALSE(DecodeImage(""));
    std::string truncated = Bytes(kPng);
    truncated.resize(truncated.size() / 2);
    REQUIRE_FALSE(DecodeImage(truncated));
    std::string jpeg = Bytes(kJpeg);
    jpeg.resize(40);
    REQUIRE_FALSE(DecodeImage(jpeg));
}

TEST_CASE("Resize averages what it shrinks, ignoring transparent pixels' colour", "[Image]") {
    RgbaImage       image{.width = 2, .height = 1, .pixels = {200, 100, 0, 255, 0, 0, 0, 0}};
    const RgbaImage one = Resize(image, 1, 1);
    REQUIRE(one.pixels == std::vector<std::uint8_t>{200, 100, 0, 127});

    const RgbaImage wide = Resize(image, 4, 2);
    REQUIRE(wide.width == 4);
    REQUIRE(wide.height == 2);
    REQUIRE(Pixel(wide, 1, 1) == std::vector<int>{200, 100, 0, 255});
    REQUIRE(Pixel(wide, 3, 0)[3] == 0);
}

TEST_CASE("Flatten composites over a background", "[Image]") {
    RgbaImage image{.width = 2, .height = 1, .pixels = {255, 255, 255, 0, 255, 0, 0, 255}};
    Flatten(image, 10, 20, 30);
    REQUIRE(image.pixels == std::vector<std::uint8_t>{10, 20, 30, 255, 255, 0, 0, 255});
}

TEST_CASE("FitToCells keeps the aspect within the box and doesn't enlarge", "[Image]") {
    // 800x400 with 10x20-pixel cells: 80 columns wide is 20 rows tall.
    REQUIRE(FitToCells(800, 400, 10, 20, 80, 30).columns == 80);
    REQUIRE(FitToCells(800, 400, 10, 20, 80, 30).rows == 20);
    // Too tall for 10 rows: narrower.
    const CellFit tall = FitToCells(800, 400, 10, 20, 80, 10);
    REQUIRE(tall.rows == 10);
    REQUIRE(tall.columns == 40);
    // A 30x30 icon stays three cells wide.
    REQUIRE(FitToCells(30, 30, 10, 20, 80, 30).columns == 3);
    REQUIRE(FitToCells(30, 30, 10, 20, 80, 30).rows == 2);
    REQUIRE(FitToCells(0, 30, 10, 20, 80, 30).columns == 0);
}
