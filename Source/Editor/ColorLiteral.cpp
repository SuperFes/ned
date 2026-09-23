#include "Editor/ColorLiteral.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <numbers>

namespace ned::editor {

namespace {

    // A functional notation longer than this is not a colour -- it is a
    // `calc()` chain or an unterminated paren, and scanning on would turn one
    // stray `rgb(` into a walk to the end of the buffer.
    constexpr std::size_t kMaxFunctionalLength = 128;

    bool IsHexDigit(char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    }

    // What counts as "still the same word", for deciding whether a candidate
    // is a colour or the middle of an identifier.
    bool IsIdentChar(char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '-';
    }

    bool IsLetter(char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    }

    char Lower(char c) {
        return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    }

    std::string ToLower(std::string_view text) {
        std::string lowered(text);
        for (char& c : lowered) {
            c = Lower(c);
        }
        return lowered;
    }

    int HexValue(char c) {
        if (c >= '0' && c <= '9') {
            return c - '0';
        }
        return Lower(c) - 'a' + 10;
    }

    double Clamp01(double value) {
        return std::clamp(value, 0.0, 1.0);
    }

    // --- the CSS named colours -------------------------------------------
    //
    // Embedded rather than read from `DataDir()`: CLAUDE.md's "bundled data is
    // read from disk" rule is about language packages and plugins, content
    // that changes per language and per install. This is a closed, frozen list
    // in a W3C specification -- the same standing as the xterm palette
    // Theme.cpp carries inline.
    //
    // Sorted by name so the lookup can binary-search; the reverse lookup
    // (colour -> name) is linear and takes the first match, which is why the
    // duplicate-valued pairs matter: `aqua`/`cyan`, `fuchsia`/`magenta` and
    // the gray/grey spellings all share a value, and the first entry in name
    // order is the one offered back.
    struct NamedColor {
        std::string_view name;
        std::uint32_t    rgb;
    };

    constexpr std::array<NamedColor, 149> kNamedColors{{
        {"aliceblue", 0xf0f8ff},
        {"antiquewhite", 0xfaebd7},
        {"aqua", 0x00ffff},
        {"aquamarine", 0x7fffd4},
        {"azure", 0xf0ffff},
        {"beige", 0xf5f5dc},
        {"bisque", 0xffe4c4},
        {"black", 0x000000},
        {"blanchedalmond", 0xffebcd},
        {"blue", 0x0000ff},
        {"blueviolet", 0x8a2be2},
        {"brown", 0xa52a2a},
        {"burlywood", 0xdeb887},
        {"cadetblue", 0x5f9ea0},
        {"chartreuse", 0x7fff00},
        {"chocolate", 0xd2691e},
        {"coral", 0xff7f50},
        {"cornflowerblue", 0x6495ed},
        {"cornsilk", 0xfff8dc},
        {"crimson", 0xdc143c},
        {"cyan", 0x00ffff},
        {"darkblue", 0x00008b},
        {"darkcyan", 0x008b8b},
        {"darkgoldenrod", 0xb8860b},
        {"darkgray", 0xa9a9a9},
        {"darkgreen", 0x006400},
        {"darkgrey", 0xa9a9a9},
        {"darkkhaki", 0xbdb76b},
        {"darkmagenta", 0x8b008b},
        {"darkolivegreen", 0x556b2f},
        {"darkorange", 0xff8c00},
        {"darkorchid", 0x9932cc},
        {"darkred", 0x8b0000},
        {"darksalmon", 0xe9967a},
        {"darkseagreen", 0x8fbc8f},
        {"darkslateblue", 0x483d8b},
        {"darkslategray", 0x2f4f4f},
        {"darkslategrey", 0x2f4f4f},
        {"darkturquoise", 0x00ced1},
        {"darkviolet", 0x9400d3},
        {"deeppink", 0xff1493},
        {"deepskyblue", 0x00bfff},
        {"dimgray", 0x696969},
        {"dimgrey", 0x696969},
        {"dodgerblue", 0x1e90ff},
        {"firebrick", 0xb22222},
        {"floralwhite", 0xfffaf0},
        {"forestgreen", 0x228b22},
        {"fuchsia", 0xff00ff},
        {"gainsboro", 0xdcdcdc},
        {"ghostwhite", 0xf8f8ff},
        {"gold", 0xffd700},
        {"goldenrod", 0xdaa520},
        {"gray", 0x808080},
        {"green", 0x008000},
        {"greenyellow", 0xadff2f},
        {"grey", 0x808080},
        {"honeydew", 0xf0fff0},
        {"hotpink", 0xff69b4},
        {"indianred", 0xcd5c5c},
        {"indigo", 0x4b0082},
        {"ivory", 0xfffff0},
        {"khaki", 0xf0e68c},
        {"lavender", 0xe6e6fa},
        {"lavenderblush", 0xfff0f5},
        {"lawngreen", 0x7cfc00},
        {"lemonchiffon", 0xfffacd},
        {"lightblue", 0xadd8e6},
        {"lightcoral", 0xf08080},
        {"lightcyan", 0xe0ffff},
        {"lightgoldenrodyellow", 0xfafad2},
        {"lightgray", 0xd3d3d3},
        {"lightgreen", 0x90ee90},
        {"lightgrey", 0xd3d3d3},
        {"lightpink", 0xffb6c1},
        {"lightsalmon", 0xffa07a},
        {"lightseagreen", 0x20b2aa},
        {"lightskyblue", 0x87cefa},
        {"lightslategray", 0x778899},
        {"lightslategrey", 0x778899},
        {"lightsteelblue", 0xb0c4de},
        {"lightyellow", 0xffffe0},
        {"lime", 0x00ff00},
        {"limegreen", 0x32cd32},
        {"linen", 0xfaf0e6},
        {"magenta", 0xff00ff},
        {"maroon", 0x800000},
        {"mediumaquamarine", 0x66cdaa},
        {"mediumblue", 0x0000cd},
        {"mediumorchid", 0xba55d3},
        {"mediumpurple", 0x9370db},
        {"mediumseagreen", 0x3cb371},
        {"mediumslateblue", 0x7b68ee},
        {"mediumspringgreen", 0x00fa9a},
        {"mediumturquoise", 0x48d1cc},
        {"mediumvioletred", 0xc71585},
        {"midnightblue", 0x191970},
        {"mintcream", 0xf5fffa},
        {"mistyrose", 0xffe4e1},
        {"moccasin", 0xffe4b5},
        {"navajowhite", 0xffdead},
        {"navy", 0x000080},
        {"oldlace", 0xfdf5e6},
        {"olive", 0x808000},
        {"olivedrab", 0x6b8e23},
        {"orange", 0xffa500},
        {"orangered", 0xff4500},
        {"orchid", 0xda70d6},
        {"palegoldenrod", 0xeee8aa},
        {"palegreen", 0x98fb98},
        {"paleturquoise", 0xafeeee},
        {"palevioletred", 0xdb7093},
        {"papayawhip", 0xffefd5},
        {"peachpuff", 0xffdab9},
        {"peru", 0xcd853f},
        {"pink", 0xffc0cb},
        {"plum", 0xdda0dd},
        {"powderblue", 0xb0e0e6},
        {"purple", 0x800080},
        {"rebeccapurple", 0x663399},
        {"red", 0xff0000},
        {"rosybrown", 0xbc8f8f},
        {"royalblue", 0x4169e1},
        {"saddlebrown", 0x8b4513},
        {"salmon", 0xfa8072},
        {"sandybrown", 0xf4a460},
        {"seagreen", 0x2e8b57},
        {"seashell", 0xfff5ee},
        {"sienna", 0xa0522d},
        {"silver", 0xc0c0c0},
        {"skyblue", 0x87ceeb},
        {"slateblue", 0x6a5acd},
        {"slategray", 0x708090},
        {"slategrey", 0x708090},
        {"snow", 0xfffafa},
        {"springgreen", 0x00ff7f},
        {"steelblue", 0x4682b4},
        {"tan", 0xd2b48c},
        {"teal", 0x008080},
        {"thistle", 0xd8bfd8},
        {"tomato", 0xff6347},
        {"transparent", 0x000000},
        {"turquoise", 0x40e0d0},
        {"violet", 0xee82ee},
        {"wheat", 0xf5deb3},
        {"white", 0xffffff},
        {"whitesmoke", 0xf5f5f5},
        {"yellow", 0xffff00},
        {"yellowgreen", 0x9acd32},
    }};

    std::optional<ColorValue> LookupNamedColor(std::string_view lowered) {
        const auto it = std::lower_bound(kNamedColors.begin(), kNamedColors.end(), lowered,
                                         [](const NamedColor& entry, std::string_view name) {
                                             return entry.name < name;
                                         });
        if (it == kNamedColors.end() || it->name != lowered) {
            return std::nullopt;
        }
        // The one keyword in the table that is not opaque.
        const double alpha = lowered == "transparent" ? 0.0 : 1.0;
        return ColorValue{.red   = static_cast<double>((it->rgb >> 16) & 0xFF) / 255.0,
                          .green = static_cast<double>((it->rgb >> 8) & 0xFF) / 255.0,
                          .blue  = static_cast<double>(it->rgb & 0xFF) / 255.0,
                          .alpha = alpha};
    }

    // --- colour space conversions ----------------------------------------

    double HueToChannel(double p, double q, double t) {
        if (t < 0.0) {
            t += 1.0;
        }
        if (t > 1.0) {
            t -= 1.0;
        }
        if (t < 1.0 / 6.0) {
            return p + ((q - p) * 6.0 * t);
        }
        if (t < 0.5) {
            return q;
        }
        if (t < 2.0 / 3.0) {
            return p + ((q - p) * ((2.0 / 3.0) - t) * 6.0);
        }
        return p;
    }

    // hue in degrees, saturation/lightness in 0..1.
    ColorValue HslToRgb(double hueDegrees, double saturation, double lightness, double alpha) {
        const double hue = std::fmod(std::fmod(hueDegrees, 360.0) + 360.0, 360.0) / 360.0;
        if (saturation <= 0.0) {
            return ColorValue{.red = lightness, .green = lightness, .blue = lightness, .alpha = alpha};
        }
        const double q = lightness < 0.5 ? lightness * (1.0 + saturation)
                                         : lightness + saturation - (lightness * saturation);
        const double p = (2.0 * lightness) - q;
        return ColorValue{.red   = HueToChannel(p, q, hue + (1.0 / 3.0)),
                          .green = HueToChannel(p, q, hue),
                          .blue  = HueToChannel(p, q, hue - (1.0 / 3.0)),
                          .alpha = alpha};
    }

    struct Hsl {
        double hue        = 0.0; // degrees
        double saturation = 0.0; // 0..1
        double lightness  = 0.0; // 0..1
    };

    Hsl RgbToHsl(const ColorValue& color) {
        const double max   = std::max({color.red, color.green, color.blue});
        const double min   = std::min({color.red, color.green, color.blue});
        const double delta = max - min;

        Hsl hsl;
        hsl.lightness = (max + min) / 2.0;
        if (delta <= 0.0) {
            return hsl;
        }
        hsl.saturation = hsl.lightness > 0.5 ? delta / (2.0 - max - min) : delta / (max + min);
        if (max == color.red) {
            hsl.hue = ((color.green - color.blue) / delta) + (color.green < color.blue ? 6.0 : 0.0);
        }
        else if (max == color.green) {
            hsl.hue = ((color.blue - color.red) / delta) + 2.0;
        }
        else {
            hsl.hue = ((color.red - color.green) / delta) + 4.0;
        }
        hsl.hue *= 60.0;
        return hsl;
    }

    // CSS Color 4's own definition: whiteness/blackness scale the fully
    // saturated hue, and a pair summing past 1 collapses to grey.
    ColorValue HwbToRgb(double hueDegrees, double whiteness, double blackness, double alpha) {
        if (whiteness + blackness >= 1.0) {
            const double grey = whiteness / (whiteness + blackness);
            return ColorValue{.red = grey, .green = grey, .blue = grey, .alpha = alpha};
        }
        ColorValue   rgb   = HslToRgb(hueDegrees, 1.0, 0.5, alpha);
        const double scale = 1.0 - whiteness - blackness;
        for (double* channel : {&rgb.red, &rgb.green, &rgb.blue}) {
            *channel = (*channel * scale) + whiteness;
        }
        return rgb;
    }

    // --- number parsing ---------------------------------------------------

    // Parses a leading decimal number, handing back whatever unit suffix
    // followed it (`%`, `deg`, `turn`, ... or empty).
    struct Number {
        double           value = 0.0;
        std::string_view unit;
    };

    std::optional<Number> ParseNumber(std::string_view token) {
        if (token.empty()) {
            return std::nullopt;
        }
        double     value = 0.0;
        const auto result =
            std::from_chars(token.data(), token.data() + token.size(), value, std::chars_format::general);
        if (result.ec != std::errc{} || result.ptr == token.data()) {
            return std::nullopt;
        }
        return Number{.value = value,
                      .unit  = token.substr(static_cast<std::size_t>(result.ptr - token.data()))};
    }

    // `<percentage> | <number>`, with the number scaled by `fullScale` --
    // 255 for an rgb() channel, 1 for a bare alpha.
    std::optional<double> ParseScaledComponent(std::string_view token, double fullScale) {
        const std::optional<Number> number = ParseNumber(token);
        if (!number) {
            return std::nullopt;
        }
        if (number->unit == "%") {
            return Clamp01(number->value / 100.0);
        }
        if (!number->unit.empty()) {
            return std::nullopt;
        }
        return Clamp01(number->value / fullScale);
    }

    // hsl()/hwb() take saturation, lightness, whiteness and blackness as
    // percentages; CSS Color 4 also admits the bare number, meaning the same.
    std::optional<double> ParsePercentComponent(std::string_view token) {
        const std::optional<Number> number = ParseNumber(token);
        if (!number || (!number->unit.empty() && number->unit != "%")) {
            return std::nullopt;
        }
        return Clamp01(number->value / 100.0);
    }

    std::optional<double> ParseAngle(std::string_view token) {
        const std::optional<Number> number = ParseNumber(token);
        if (!number) {
            return std::nullopt;
        }
        if (number->unit.empty() || number->unit == "deg") {
            return number->value;
        }
        if (number->unit == "turn") {
            return number->value * 360.0;
        }
        if (number->unit == "rad") {
            return number->value * 180.0 / std::numbers::pi;
        }
        if (number->unit == "grad") {
            return number->value * 0.9;
        }
        return std::nullopt;
    }

    // --- the recognisers --------------------------------------------------

    ColorValue HexToColor(std::string_view digits) {
        const bool   shortForm   = digits.size() <= 4;
        const double channels[4] = {
            shortForm ? HexValue(digits[0]) * 17.0 : (HexValue(digits[0]) * 16.0) + HexValue(digits[1]),
            shortForm ? HexValue(digits[1]) * 17.0 : (HexValue(digits[2]) * 16.0) + HexValue(digits[3]),
            shortForm ? HexValue(digits[2]) * 17.0 : (HexValue(digits[4]) * 16.0) + HexValue(digits[5]),
            [&] {
                if (digits.size() == 4) {
                    return HexValue(digits[3]) * 17.0;
                }
                if (digits.size() == 8) {
                    return (HexValue(digits[6]) * 16.0) + HexValue(digits[7]);
                }
                return 255.0;
            }(),
        };
        return ColorValue{.red   = channels[0] / 255.0,
                          .green = channels[1] / 255.0,
                          .blue  = channels[2] / 255.0,
                          .alpha = channels[3] / 255.0};
    }

    // The values inside a functional notation, split on CSS's two
    // interchangeable separator styles (commas, or whitespace with the alpha
    // behind a slash).
    struct FunctionalArguments {
        std::vector<std::string_view>   values;
        std::optional<std::string_view> alpha;
    };

    std::optional<FunctionalArguments> SplitFunctionalArguments(std::string_view inner) {
        FunctionalArguments           arguments;
        std::vector<std::string_view> afterSlash;
        bool                          sawSlash = false;

        std::size_t i = 0;
        while (i < inner.size()) {
            const char c = inner[i];
            if (c == ' ' || c == '\t' || c == ',') {
                ++i;
                continue;
            }
            if (c == '/') {
                if (sawSlash) {
                    return std::nullopt;
                }
                sawSlash = true;
                ++i;
                continue;
            }
            const std::size_t start = i;
            while (i < inner.size() && inner[i] != ' ' && inner[i] != '\t' && inner[i] != ',' && inner[i] != '/') {
                ++i;
            }
            (sawSlash ? afterSlash : arguments.values).push_back(inner.substr(start, i - start));
        }

        if (sawSlash) {
            if (afterSlash.size() != 1) {
                return std::nullopt;
            }
            arguments.alpha = afterSlash.front();
        }
        else if (arguments.values.size() == 4) {
            // The legacy rgba()/hsla() shape: the fourth positional value is
            // the alpha.
            arguments.alpha = arguments.values.back();
            arguments.values.pop_back();
        }
        if (arguments.values.size() != 3) {
            return std::nullopt;
        }
        return arguments;
    }

    std::optional<ColorValue> ParseFunctionalColor(std::string_view name, std::string_view inner) {
        const std::optional<FunctionalArguments> arguments = SplitFunctionalArguments(inner);
        if (!arguments) {
            return std::nullopt;
        }

        double alpha = 1.0;
        if (arguments->alpha) {
            const std::optional<double> parsed = ParseScaledComponent(*arguments->alpha, 1.0);
            if (!parsed) {
                return std::nullopt;
            }
            alpha = *parsed;
        }

        if (name == "rgb" || name == "rgba") {
            const std::optional<double> red   = ParseScaledComponent(arguments->values[0], 255.0);
            const std::optional<double> green = ParseScaledComponent(arguments->values[1], 255.0);
            const std::optional<double> blue  = ParseScaledComponent(arguments->values[2], 255.0);
            if (!red || !green || !blue) {
                return std::nullopt;
            }
            return ColorValue{.red = *red, .green = *green, .blue = *blue, .alpha = alpha};
        }

        const std::optional<double> hue    = ParseAngle(arguments->values[0]);
        const std::optional<double> second = ParsePercentComponent(arguments->values[1]);
        const std::optional<double> third  = ParsePercentComponent(arguments->values[2]);
        if (!hue || !second || !third) {
            return std::nullopt;
        }
        if (name == "hwb") {
            return HwbToRgb(*hue, *second, *third, alpha);
        }
        return HslToRgb(*hue, *second, *third, alpha);
    }

    std::optional<ColorSyntax> FunctionalSyntaxFor(std::string_view name) {
        if (name == "rgb") {
            return ColorSyntax::Rgb;
        }
        if (name == "rgba") {
            return ColorSyntax::Rgba;
        }
        if (name == "hsl") {
            return ColorSyntax::Hsl;
        }
        if (name == "hsla") {
            return ColorSyntax::Hsla;
        }
        if (name == "hwb") {
            return ColorSyntax::Hwb;
        }
        return std::nullopt;
    }

    // --- formatting -------------------------------------------------------

    std::string FormatDouble(double value, int decimals) {
        std::array<char, 32> scratch{};
        const int            written = std::snprintf(scratch.data(), scratch.size(), "%.*f", decimals, value);
        std::string          text(scratch.data(), static_cast<std::size_t>(std::max(0, written)));
        if (text.find('.') != std::string::npos) {
            text.erase(text.find_last_not_of('0') + 1);
            if (!text.empty() && text.back() == '.') {
                text.pop_back();
            }
        }
        return text;
    }

    // Alpha at the coarsest precision that still round-trips to the same
    // 8-bit value -- so `#ff000080` offers `rgba(255, 0, 0, 0.5)` rather than
    // `0.502`, and a genuinely odd alpha keeps its digits.
    std::string FormatAlpha(double alpha) {
        const std::uint8_t target = ColorChannelToByte(alpha);
        for (int decimals = 1; decimals < 4; ++decimals) {
            const std::string candidate = FormatDouble(alpha, decimals);
            const auto        parsed    = ParseNumber(candidate);
            if (parsed && ColorChannelToByte(parsed->value) == target) {
                return candidate;
            }
        }
        return FormatDouble(alpha, 4);
    }

    std::string HexDigits(const ColorValue& color, bool withAlpha) {
        std::array<char, 16> scratch{};
        const int            written =
            withAlpha ? std::snprintf(scratch.data(), scratch.size(), "#%02x%02x%02x%02x",
                                      ColorChannelToByte(color.red), ColorChannelToByte(color.green),
                                      ColorChannelToByte(color.blue), ColorChannelToByte(color.alpha))
                      : std::snprintf(scratch.data(), scratch.size(), "#%02x%02x%02x", ColorChannelToByte(color.red),
                                      ColorChannelToByte(color.green), ColorChannelToByte(color.blue));
        return std::string(scratch.data(), static_cast<std::size_t>(std::max(0, written)));
    }

    bool NibblesRepeat(std::uint8_t byte) {
        return (byte >> 4) == (byte & 0x0F);
    }

    // Both hue-based notations round to whole degrees and whole percents when
    // that still reproduces the original 8-bit colour, and carry two decimals
    // when it does not -- accepting a presentation must never shift the
    // colour it was offered for.
    struct HueComponents {
        double hue    = 0.0;
        double first  = 0.0; // saturation, or whiteness
        double second = 0.0; // lightness, or blackness
    };

    bool RoundTripsAtWholeNumbers(const ColorValue& color, const HueComponents& parts, bool isHwb) {
        const double     hue    = std::round(parts.hue);
        const double     first  = std::round(parts.first * 100.0) / 100.0;
        const double     second = std::round(parts.second * 100.0) / 100.0;
        const ColorValue back   = isHwb ? HwbToRgb(hue, first, second, color.alpha)
                                        : HslToRgb(hue, first, second, color.alpha);
        return ColorChannelToByte(back.red) == ColorChannelToByte(color.red) &&
               ColorChannelToByte(back.green) == ColorChannelToByte(color.green) &&
               ColorChannelToByte(back.blue) == ColorChannelToByte(color.blue);
    }

    std::string FormatHueNotation(const ColorValue& color, const HueComponents& parts, bool isHwb, bool withAlpha) {
        const bool  whole    = RoundTripsAtWholeNumbers(color, parts, isHwb);
        const int   decimals = whole ? 0 : 2;
        std::string hue      = FormatDouble(whole ? std::round(parts.hue) : parts.hue, decimals);
        std::string first    = FormatDouble(parts.first * 100.0, decimals);
        std::string second   = FormatDouble(parts.second * 100.0, decimals);
        if (!whole) {
            first  = FormatDouble(parts.first * 100.0, 2);
            second = FormatDouble(parts.second * 100.0, 2);
        }

        if (isHwb) {
            // hwb() has no comma form in CSS at all.
            std::string text = "hwb(" + hue + " " + first + "% " + second + "%";
            if (withAlpha) {
                text += " / " + FormatAlpha(color.alpha);
            }
            return text + ")";
        }
        if (withAlpha) {
            return "hsla(" + hue + ", " + first + "%, " + second + "%, " + FormatAlpha(color.alpha) + ")";
        }
        return "hsl(" + hue + ", " + first + "%, " + second + "%)";
    }

    bool IsOpaque(const ColorValue& color) {
        return ColorChannelToByte(color.alpha) == 255;
    }

} // namespace

std::uint8_t ColorChannelToByte(double component) {
    return static_cast<std::uint8_t>(std::lround(component * 255.0));
}

std::vector<ColorLiteral> ScanColorLiterals(std::string_view text, const ColorLiteralOptions& options) {
    std::vector<ColorLiteral> literals;

    std::size_t i = 0;
    while (i < text.size()) {
        const char c = text[i];

        if (c == '#') {
            std::size_t end = i + 1;
            while (end < text.size() && IsHexDigit(text[end])) {
                ++end;
            }
            const std::size_t length = end - i - 1;
            // A hex run is a colour only when nothing word-like follows it:
            // `#deadbeefcafe` is an identifier, not a colour with a tail.
            const bool                 bounded = end >= text.size() || (!IsLetter(text[end]) && !std::isdigit(static_cast<unsigned char>(text[end])) && text[end] != '_');
            std::optional<ColorSyntax> syntax;
            if (bounded) {
                if (length == 6) {
                    syntax = ColorSyntax::Hex;
                }
                else if (length == 8) {
                    syntax = ColorSyntax::HexAlpha;
                }
                else if (length == 3 && options.shortHex) {
                    syntax = ColorSyntax::HexShort;
                }
                else if (length == 4 && options.shortHex) {
                    syntax = ColorSyntax::HexShortAlpha;
                }
            }
            if (syntax) {
                literals.push_back(ColorLiteral{.begin  = i,
                                                .end    = end,
                                                .color  = HexToColor(text.substr(i + 1, length)),
                                                .syntax = *syntax});
            }
            i = end;
            continue;
        }

        if (!IsLetter(c) || (i > 0 && IsIdentChar(text[i - 1]))) {
            ++i;
            continue;
        }

        std::size_t wordEnd = i;
        while (wordEnd < text.size() && IsIdentChar(text[wordEnd])) {
            ++wordEnd;
        }
        const std::string_view word = text.substr(i, wordEnd - i);
        const bool             allLetters =
            std::all_of(word.begin(), word.end(), [](char ch) { return IsLetter(ch); });
        if (!allLetters) {
            i = wordEnd;
            continue;
        }
        const std::string lowered = ToLower(word);

        if (wordEnd < text.size() && text[wordEnd] == '(') {
            const std::optional<ColorSyntax> syntax = FunctionalSyntaxFor(lowered);
            if (!syntax) {
                i = wordEnd;
                continue;
            }
            const std::size_t limit = std::min(text.size(), wordEnd + kMaxFunctionalLength);
            std::size_t       close = wordEnd + 1;
            while (close < limit && text[close] != ')' && text[close] != '(') {
                ++close;
            }
            if (close >= limit || text[close] != ')') {
                i = wordEnd;
                continue;
            }
            const std::optional<ColorValue> color =
                ParseFunctionalColor(lowered, text.substr(wordEnd + 1, close - wordEnd - 1));
            if (color) {
                literals.push_back(
                    ColorLiteral{.begin = i, .end = close + 1, .color = *color, .syntax = *syntax});
                i = close + 1;
                continue;
            }
            i = wordEnd;
            continue;
        }

        if (options.namedColors) {
            if (const std::optional<ColorValue> color = LookupNamedColor(lowered)) {
                literals.push_back(
                    ColorLiteral{.begin = i, .end = wordEnd, .color = *color, .syntax = ColorSyntax::Named});
            }
        }
        i = wordEnd;
    }

    return literals;
}

const ColorLiteral* ColorLiteralContaining(const std::vector<ColorLiteral>& literals, std::size_t offset) {
    for (const ColorLiteral& literal : literals) {
        if (offset >= literal.begin && offset <= literal.end) {
            return &literal;
        }
        if (literal.begin > offset) {
            break;
        }
    }
    return nullptr;
}

std::optional<std::string> FormatColor(const ColorValue& color, ColorSyntax syntax) {
    const bool opaque = IsOpaque(color);

    switch (syntax) {
        case ColorSyntax::Hex:
            return opaque ? std::optional<std::string>(HexDigits(color, false)) : std::nullopt;
        case ColorSyntax::HexAlpha:
            return HexDigits(color, true);
        case ColorSyntax::HexShort:
        case ColorSyntax::HexShortAlpha: {
            const bool withAlpha = syntax == ColorSyntax::HexShortAlpha;
            if (!withAlpha && !opaque) {
                return std::nullopt;
            }
            const std::uint8_t bytes[4] = {ColorChannelToByte(color.red), ColorChannelToByte(color.green),
                                           ColorChannelToByte(color.blue), ColorChannelToByte(color.alpha)};
            const int          count    = withAlpha ? 4 : 3;
            std::string        text     = "#";
            for (int channel = 0; channel < count; ++channel) {
                if (!NibblesRepeat(bytes[channel])) {
                    return std::nullopt;
                }
                text += "0123456789abcdef"[bytes[channel] & 0x0F];
            }
            return text;
        }
        case ColorSyntax::Rgb:
        case ColorSyntax::Rgba: {
            const bool withAlpha = syntax == ColorSyntax::Rgba;
            if (!withAlpha && !opaque) {
                return std::nullopt;
            }
            std::string text = withAlpha ? "rgba(" : "rgb(";
            text += std::to_string(ColorChannelToByte(color.red)) + ", " +
                    std::to_string(ColorChannelToByte(color.green)) + ", " +
                    std::to_string(ColorChannelToByte(color.blue));
            if (withAlpha) {
                text += ", " + FormatAlpha(color.alpha);
            }
            return text + ")";
        }
        case ColorSyntax::Hsl:
        case ColorSyntax::Hsla: {
            const bool withAlpha = syntax == ColorSyntax::Hsla;
            if (!withAlpha && !opaque) {
                return std::nullopt;
            }
            const Hsl hsl = RgbToHsl(color);
            return FormatHueNotation(
                color, HueComponents{.hue = hsl.hue, .first = hsl.saturation, .second = hsl.lightness}, false,
                withAlpha);
        }
        case ColorSyntax::Hwb: {
            const Hsl    hsl       = RgbToHsl(color);
            const double whiteness = std::min({color.red, color.green, color.blue});
            const double blackness = 1.0 - std::max({color.red, color.green, color.blue});
            return FormatHueNotation(color,
                                     HueComponents{.hue = hsl.hue, .first = whiteness, .second = blackness}, true,
                                     !opaque);
        }
        case ColorSyntax::Unknown:
            return std::nullopt;
        case ColorSyntax::Named: {
            const std::uint8_t red   = ColorChannelToByte(color.red);
            const std::uint8_t green = ColorChannelToByte(color.green);
            const std::uint8_t blue  = ColorChannelToByte(color.blue);
            if (!opaque) {
                // `transparent` is the one keyword that can name a
                // non-opaque colour, and only that exact one.
                if (ColorChannelToByte(color.alpha) == 0 && red == 0 && green == 0 && blue == 0) {
                    return std::string("transparent");
                }
                return std::nullopt;
            }
            const std::uint32_t packed =
                (static_cast<std::uint32_t>(red) << 16) | (static_cast<std::uint32_t>(green) << 8) | blue;
            for (const NamedColor& entry : kNamedColors) {
                if (entry.rgb == packed && entry.name != "transparent") {
                    return std::string(entry.name);
                }
            }
            return std::nullopt;
        }
    }
    return std::nullopt;
}

std::vector<ColorPresentation> ColorPresentations(const ColorValue&          color,
                                                  const ColorLiteralOptions& options) {
    const bool opaque = IsOpaque(color);

    std::vector<ColorSyntax> candidates;
    if (opaque) {
        candidates = {ColorSyntax::Hex, ColorSyntax::HexShort, ColorSyntax::Rgb, ColorSyntax::Hsl,
                      ColorSyntax::Hwb, ColorSyntax::Named};
    }
    else {
        candidates = {ColorSyntax::HexAlpha, ColorSyntax::HexShortAlpha, ColorSyntax::Rgba, ColorSyntax::Hsla,
                      ColorSyntax::Hwb, ColorSyntax::Named};
    }

    std::vector<ColorPresentation> presentations;
    for (const ColorSyntax syntax : candidates) {
        if (!options.shortHex && (syntax == ColorSyntax::HexShort || syntax == ColorSyntax::HexShortAlpha)) {
            continue;
        }
        if (!options.namedColors && syntax == ColorSyntax::Named) {
            continue;
        }
        if (std::optional<std::string> text = FormatColor(color, syntax)) {
            presentations.push_back(ColorPresentation{.syntax = syntax, .text = std::move(*text)});
        }
    }
    return presentations;
}

} // namespace ned::editor
