#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "Editor/ColorLiteral.h"

using ned::editor::ColorLiteral;
using ned::editor::ColorLiteralContaining;
using ned::editor::ColorLiteralOptions;
using ned::editor::ColorPresentation;
using ned::editor::ColorPresentations;
using ned::editor::ColorSyntax;
using ned::editor::ColorValue;
using ned::editor::FormatColor;
using ned::editor::ScanColorLiterals;

namespace {

// The universal set -- what every language gets without opting in.
constexpr ColorLiteralOptions kPlain{};

// What a stylesheet language declares.
constexpr ColorLiteralOptions kStylesheet{.shortHex = true, .namedColors = true};

std::string TextOf(std::string_view source, const ColorLiteral& literal) {
    return std::string(source.substr(literal.begin, literal.end - literal.begin));
}

// Comparing doubles channel-by-channel is not what any consumer cares about;
// every one of them paints or formats 8-bit sRGB.
std::string HexOf(const ColorValue& color) {
    return FormatColor(color, ColorSyntax::HexAlpha).value_or("<none>");
}

} // namespace

TEST_CASE("ScanColorLiterals finds 6- and 8-digit hex in any language", "[ColorLiteral]") {
    const std::string               source = R"(background: #ff00aa; border: #11223344;)";
    const std::vector<ColorLiteral> found  = ScanColorLiterals(source, kPlain);

    REQUIRE(found.size() == 2);
    CHECK(TextOf(source, found[0]) == "#ff00aa");
    CHECK(found[0].syntax == ColorSyntax::Hex);
    CHECK(HexOf(found[0].color) == "#ff00aaff");
    CHECK(TextOf(source, found[1]) == "#11223344");
    CHECK(found[1].syntax == ColorSyntax::HexAlpha);
    CHECK(HexOf(found[1].color) == "#11223344");
}

TEST_CASE("ScanColorLiterals leaves short hex to languages that opt in", "[ColorLiteral]") {
    // `#abc` is how a comment starts in half the languages ned parses, which
    // is the whole reason this spelling is not universal.
    const std::string source = "#f0a and #f0ab";

    CHECK(ScanColorLiterals(source, kPlain).empty());

    const std::vector<ColorLiteral> found = ScanColorLiterals(source, kStylesheet);
    REQUIRE(found.size() == 2);
    CHECK(found[0].syntax == ColorSyntax::HexShort);
    CHECK(HexOf(found[0].color) == "#ff00aaff");
    CHECK(found[1].syntax == ColorSyntax::HexShortAlpha);
    CHECK(HexOf(found[1].color) == "#ff00aabb");
}

TEST_CASE("A hex run with a word-like tail is an identifier, not a colour", "[ColorLiteral]") {
    CHECK(ScanColorLiterals("#deadbeefcafe", kStylesheet).empty());
    CHECK(ScanColorLiterals("#ff00aaz", kPlain).empty());
    CHECK(ScanColorLiterals("#ff00aa_", kPlain).empty());
    CHECK(ScanColorLiterals("#fffff", kStylesheet).empty()); // five digits is no spelling at all

    // Ordinary punctuation after a literal is not a tail.
    CHECK(ScanColorLiterals("#ff00aa;", kPlain).size() == 1);
    CHECK(ScanColorLiterals("\"#ff00aa\"", kPlain).size() == 1);
}

TEST_CASE("ScanColorLiterals reads both CSS separator styles", "[ColorLiteral]") {
    struct Case {
        std::string text;
        std::string expected;
        ColorSyntax syntax;
    };

    const std::vector<Case> cases{
        {"rgb(255, 0, 170)", "#ff00aaff", ColorSyntax::Rgb},
        {"rgb(255 0 170)", "#ff00aaff", ColorSyntax::Rgb},
        {"rgba(255, 0, 170, 0.5)", "#ff00aa80", ColorSyntax::Rgba},
        {"rgb(255 0 170 / 50%)", "#ff00aa80", ColorSyntax::Rgb},
        {"rgb(100%, 0%, 66.67%)", "#ff00aaff", ColorSyntax::Rgb},
        {"hsl(320, 100%, 50%)", "#ff00aaff", ColorSyntax::Hsl},
        {"hsl(320deg 100% 50%)", "#ff00aaff", ColorSyntax::Hsl},
        {"hsla(320, 100%, 50%, 0.5)", "#ff00aa80", ColorSyntax::Hsla},
        {"hwb(320 0% 0%)", "#ff00aaff", ColorSyntax::Hwb},
        {"HSL(320, 100%, 50%)", "#ff00aaff", ColorSyntax::Hsl},
    };

    for (const Case& test : cases) {
        const std::vector<ColorLiteral> found = ScanColorLiterals(test.text, kPlain);
        INFO(test.text);
        REQUIRE(found.size() == 1);
        CHECK(TextOf(test.text, found[0]) == test.text);
        CHECK(found[0].syntax == test.syntax);
        CHECK(HexOf(found[0].color) == test.expected);
    }
}

TEST_CASE("Angle units other than degrees are honoured", "[ColorLiteral]") {
    for (const std::string text : {"hsl(0.8889turn, 100%, 50%)", "hsl(355.56grad, 100%, 50%)"}) {
        const std::vector<ColorLiteral> found = ScanColorLiterals(text, kPlain);
        INFO(text);
        REQUIRE(found.size() == 1);
        CHECK(HexOf(found[0].color) == "#ff00aaff");
    }
}

TEST_CASE("A functional notation ned cannot evaluate is not a colour", "[ColorLiteral]") {
    // Nested functions, wrong arity and unterminated parens all fall through
    // rather than producing a wrong swatch.
    CHECK(ScanColorLiterals("rgb(calc(1 + 1), 0, 0)", kPlain).empty());
    CHECK(ScanColorLiterals("rgb(255, 0)", kPlain).empty());
    CHECK(ScanColorLiterals("rgb(255, 0, 170", kPlain).empty());
    CHECK(ScanColorLiterals("rgb(255 0 170 / 0.5 / 0.5)", kPlain).empty());
    CHECK(ScanColorLiterals("oklch(70% 0.1 200)", kPlain).empty());
}

TEST_CASE("Named colours are stylesheet-only, and never a fragment of an identifier", "[ColorLiteral]") {
    CHECK(ScanColorLiterals("color: red;", kPlain).empty());

    const std::string               source = "color: red;";
    const std::vector<ColorLiteral> found  = ScanColorLiterals(source, kStylesheet);
    REQUIRE(found.size() == 1);
    CHECK(TextOf(source, found[0]) == "red");
    CHECK(found[0].syntax == ColorSyntax::Named);
    CHECK(HexOf(found[0].color) == "#ff0000ff");

    CHECK(ScanColorLiterals("infrared", kStylesheet).empty());
    CHECK(ScanColorLiterals("red_herring", kStylesheet).empty());
    CHECK(ScanColorLiterals("--red-dark", kStylesheet).empty());

    const std::vector<ColorLiteral> transparent = ScanColorLiterals("transparent", kStylesheet);
    REQUIRE(transparent.size() == 1);
    CHECK(HexOf(transparent[0].color) == "#00000000");
}

TEST_CASE("ColorLiteralContaining finds the literal point sits in, including its far edge", "[ColorLiteral]") {
    const std::string               source   = "a: #ff00aa;";
    const std::vector<ColorLiteral> literals = ScanColorLiterals(source, kPlain);

    CHECK(ColorLiteralContaining(literals, 0) == nullptr);
    CHECK(ColorLiteralContaining(literals, 2) == nullptr);
    CHECK(ColorLiteralContaining(literals, 3) != nullptr);  // on the '#'
    CHECK(ColorLiteralContaining(literals, 7) != nullptr);  // mid-literal
    CHECK(ColorLiteralContaining(literals, 10) != nullptr); // just past the last digit
    CHECK(ColorLiteralContaining(literals, 11) == nullptr);
    CHECK(ColorLiteralContaining({}, 3) == nullptr);
}

TEST_CASE("FormatColor refuses every spelling that would drop the alpha", "[ColorLiteral]") {
    const ColorValue translucent{.red = 1.0, .green = 0.0, .blue = 2.0 / 3.0, .alpha = 0.5};

    CHECK(!FormatColor(translucent, ColorSyntax::Hex));
    CHECK(!FormatColor(translucent, ColorSyntax::Rgb));
    CHECK(!FormatColor(translucent, ColorSyntax::Hsl));
    CHECK(!FormatColor(translucent, ColorSyntax::Named));
    CHECK(FormatColor(translucent, ColorSyntax::HexAlpha));
    CHECK(FormatColor(translucent, ColorSyntax::Rgba));
}

TEST_CASE("FormatColor writes short hex only when every channel repeats its nibble", "[ColorLiteral]") {
    const ColorValue exact{.red = 1.0, .green = 0.0, .blue = 2.0 / 3.0, .alpha = 1.0};
    CHECK(FormatColor(exact, ColorSyntax::HexShort) == "#f0a");

    const ColorValue inexact{.red = 1.0, .green = 0.0, .blue = 0.5, .alpha = 1.0};
    CHECK(!FormatColor(inexact, ColorSyntax::HexShort));
}

TEST_CASE("FormatColor keeps precision when whole numbers would shift the colour", "[ColorLiteral]") {
    // Every 8-bit colour, spelled as hsl() and read back, must land on the
    // same 8-bit colour -- that is the whole contract of offering the
    // conversion as a presentation.
    for (int red = 0; red < 256; red += 7) {
        for (int green = 0; green < 256; green += 11) {
            for (int blue = 0; blue < 256; blue += 13) {
                const ColorValue color{.red   = red / 255.0,
                                       .green = green / 255.0,
                                       .blue  = blue / 255.0,
                                       .alpha = 1.0};
                for (const ColorSyntax syntax : {ColorSyntax::Hsl, ColorSyntax::Hwb}) {
                    const std::optional<std::string> text = FormatColor(color, syntax);
                    REQUIRE(text);
                    INFO(*text);
                    const std::vector<ColorLiteral> reparsed = ScanColorLiterals(*text, kPlain);
                    REQUIRE(reparsed.size() == 1);
                    CHECK(HexOf(reparsed[0].color) == HexOf(color));
                }
            }
        }
    }
}

TEST_CASE("Every alpha byte round-trips through rgba()'s decimal alpha", "[ColorLiteral]") {
    for (int alpha = 0; alpha < 256; ++alpha) {
        const ColorValue                 color{.red = 1.0, .green = 0.0, .blue = 0.0, .alpha = alpha / 255.0};
        const std::optional<std::string> text = FormatColor(color, ColorSyntax::Rgba);
        REQUIRE(text);
        INFO(*text);
        const std::vector<ColorLiteral> reparsed = ScanColorLiterals(*text, kPlain);
        REQUIRE(reparsed.size() == 1);
        CHECK(HexOf(reparsed[0].color) == HexOf(color));
    }
}

TEST_CASE("ColorPresentations offers only spellings the same options would scan back", "[ColorLiteral]") {
    const ColorValue red{.red = 1.0, .green = 0.0, .blue = 0.0, .alpha = 1.0};

    const std::vector<ColorPresentation> plain = ColorPresentations(red, kPlain);
    for (const ColorPresentation& presentation : plain) {
        CHECK(presentation.syntax != ColorSyntax::HexShort);
        CHECK(presentation.syntax != ColorSyntax::Named);
    }

    const std::vector<ColorPresentation> stylesheet = ColorPresentations(red, kStylesheet);
    std::vector<std::string>             texts;
    for (const ColorPresentation& presentation : stylesheet) {
        texts.push_back(presentation.text);
    }
    CHECK(texts == std::vector<std::string>{"#ff0000", "#f00", "rgb(255, 0, 0)", "hsl(0, 100%, 50%)",
                                            "hwb(0 0% 0%)", "red"});
}

TEST_CASE("Every presentation re-scans to the colour it was offered for", "[ColorLiteral]") {
    const std::vector<ColorValue> colors{
        {.red = 1.0, .green = 0.0, .blue = 2.0 / 3.0, .alpha = 1.0},
        {.red = 0.4, .green = 0.2, .blue = 0.6, .alpha = 0.5},
        {.red = 0.0, .green = 0.0, .blue = 0.0, .alpha = 0.0},
        {.red = 1.0, .green = 1.0, .blue = 1.0, .alpha = 1.0},
        {.red = 0.2, .green = 0.2, .blue = 0.2, .alpha = 1.0},
    };

    for (const ColorValue& color : colors) {
        for (const ColorPresentation& presentation : ColorPresentations(color, kStylesheet)) {
            INFO(presentation.text);
            const std::vector<ColorLiteral> found = ScanColorLiterals(presentation.text, kStylesheet);
            REQUIRE(found.size() == 1);
            CHECK(found[0].begin == 0);
            CHECK(found[0].end == presentation.text.size());
            CHECK(HexOf(found[0].color) == HexOf(color));
        }
    }
}
