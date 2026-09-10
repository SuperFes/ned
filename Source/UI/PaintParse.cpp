#include "PaintParse.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>

#include "Compositing.h"
#include "Theme.h"

namespace ned::ui {

namespace {

    void Fail(std::string* error, std::string message) {
        if (error != nullptr) {
            *error = std::move(message);
        }
    }

    // Keywords may arrive with the scripting language's own keyword marker
    // still attached (Janet's :x), or without it from the string form.
    std::string_view StripKeyword(std::string_view token) {
        return token.starts_with(':') ? token.substr(1) : token;
    }

    std::optional<PaintAxis> AxisNamed(std::string_view name) {
        if (name == "x") {
            return PaintAxis::X;
        }
        if (name == "y") {
            return PaintAxis::Y;
        }
        if (name == "diag") {
            return PaintAxis::Diag;
        }
        if (name == "radial") {
            return PaintAxis::Radial;
        }
        return std::nullopt;
    }

    std::optional<PatternKind> PatternNamed(std::string_view name) {
        if (name == "checker") {
            return PatternKind::Checker;
        }
        if (name == "stripes") {
            return PatternKind::Stripes;
        }
        if (name == "scanlines") {
            return PatternKind::Scanlines;
        }
        if (name == "grid") {
            return PatternKind::Grid;
        }
        if (name == "hatch") {
            return PatternKind::Hatch;
        }
        if (name == "dots") {
            return PatternKind::Dots;
        }
        if (name == "noise") {
            return PatternKind::Noise;
        }
        return std::nullopt;
    }

    const char* AxisName(PaintAxis axis) {
        switch (axis) {
            case PaintAxis::X:
                return "x";
            case PaintAxis::Diag:
                return "diag";
            case PaintAxis::Radial:
                return "radial";
            case PaintAxis::Y:
            default:
                return "y";
        }
    }

    const char* PatternName(PatternKind pattern) {
        switch (pattern) {
            case PatternKind::Stripes:
                return "stripes";
            case PatternKind::Scanlines:
                return "scanlines";
            case PatternKind::Grid:
                return "grid";
            case PatternKind::Hatch:
                return "hatch";
            case PatternKind::Dots:
                return "dots";
            case PatternKind::Noise:
                return "noise";
            case PatternKind::Checker:
            default:
                return "checker";
        }
    }

    // "60%" -- a fade stop. Percentages are how a Fade says how much of the
    // colour already there survives, and being a *string* is what keeps
    // them distinguishable from the numbers that mean weights.
    std::optional<double> ParsePercentStop(std::string_view token) {
        if (token.size() < 2 || token.back() != '%') {
            return std::nullopt;
        }
        const std::string digits(token.substr(0, token.size() - 1));
        try {
            std::size_t  consumed = 0;
            const double value    = std::stod(digits, &consumed);
            if (consumed != digits.size()) {
                return std::nullopt;
            }
            return std::clamp(value / 100.0, 0.0, 1.0);
        }
        catch (const std::exception&) {
            return std::nullopt;
        }
    }

    Color AdjustLightness(const Color& colour, double percent) {
        if (!colour.Composable() || percent == 0.0) {
            return colour;
        }
        // Toward white or black by `percent`, which keeps a slot's own hue
        // rather than reaching for a fixed pair of colours.
        const Color target = percent > 0 ? Color::RGB(0xFFFFFF) : Color::RGB(0x000000);
        const auto  amount = static_cast<std::uint8_t>(std::lround(std::clamp(std::abs(percent), 0.0, 100.0) * 2.55));
        const Color moved  = BlendOver(colour, target.WithAlpha(amount));
        return moved.WithAlpha(colour.alpha);
    }

} // namespace

std::optional<Color> ParseColorStop(std::string_view token, const PaintContext& context) {
    if (token.empty()) {
        return std::nullopt;
    }
    if (token[0] != '$') {
        return ParseColorToken(token);
    }

    // $slot with up to three postfix adjustments, applied left to right.
    // '-' only starts an adjustment when a digit follows it, so a slot may
    // still have a hyphen in its name ($chrome-bg) without being read as
    // "$chrome darkened by bg".
    std::size_t nameEnd = 1;
    while (nameEnd < token.size()) {
        const char c = token[nameEnd];
        if (c == '+' || c == '/') {
            break;
        }
        if (c == '-' && nameEnd + 1 < token.size() &&
            std::isdigit(static_cast<unsigned char>(token[nameEnd + 1])) != 0) {
            break;
        }
        ++nameEnd;
    }
    const std::string_view name = token.substr(1, nameEnd - 1);
    if (name.empty() || !context.slot) {
        return std::nullopt;
    }
    std::optional<Color> colour = context.slot(name);
    if (!colour) {
        return std::nullopt;
    }

    std::size_t at = nameEnd;
    while (at < token.size()) {
        const char  op        = token[at++];
        std::size_t digitsEnd = at;
        while (digitsEnd < token.size() && (std::isdigit(static_cast<unsigned char>(token[digitsEnd])) != 0 ||
                                            token[digitsEnd] == '.')) {
            ++digitsEnd;
        }
        if (digitsEnd == at) {
            return std::nullopt;
        }
        const std::string digits(token.substr(at, digitsEnd - at));
        double            value = 0.0;
        try {
            value = std::stod(digits);
        }
        catch (const std::exception&) {
            return std::nullopt;
        }
        at = digitsEnd;

        switch (op) {
            case '+':
                colour = AdjustLightness(*colour, value);
                break;
            case '-':
                colour = AdjustLightness(*colour, -value);
                break;
            case '/':
                colour = colour->WithAlpha(
                    static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0, 100.0) * 2.55)));
                break;
            default:
                return std::nullopt;
        }
    }
    return colour;
}

std::optional<Paint> ParsePaint(const std::vector<PaintToken>& tokens, const PaintContext& context,
                                std::string* error) {
    if (tokens.empty()) {
        Fail(error, "empty paint");
        return std::nullopt;
    }

    std::size_t at = 0;

    // --- leading keyword: axis, pattern, blur, or stack -----------------
    PaintAxis                  axis = PaintAxis::Y;
    std::optional<PatternKind> pattern;
    bool                       isBlur  = false;
    bool                       isStack = false;

    if (tokens[at].kind == PaintToken::Kind::Text) {
        const std::string_view keyword = StripKeyword(tokens[at].text);
        if (const auto named = AxisNamed(keyword)) {
            axis = *named;
            ++at;
        }
        else if (const auto kind = PatternNamed(keyword)) {
            pattern = *kind;
            ++at;
        }
        else if (keyword == "blur") {
            isBlur = true;
            ++at;
        }
        else if (keyword == "stack") {
            isStack = true;
            ++at;
        }
    }

    if (isStack) {
        std::vector<Paint> layers;
        for (; at < tokens.size(); ++at) {
            if (tokens[at].kind == PaintToken::Kind::Nested) {
                const std::optional<Paint> layer = ParsePaint(tokens[at].nested, context, error);
                if (!layer) {
                    return std::nullopt;
                }
                layers.push_back(*layer);
                continue;
            }
            if (tokens[at].kind == PaintToken::Kind::Text) {
                // A bare named paint is a legal layer: [:stack "$glass" ...]
                const std::optional<Paint> layer = ParsePaint({tokens[at]}, context, error);
                if (!layer) {
                    return std::nullopt;
                }
                layers.push_back(*layer);
                continue;
            }
            Fail(error, "a stack's layers must be paints, not weights");
            return std::nullopt;
        }
        if (layers.empty()) {
            Fail(error, "stack with no layers");
            return std::nullopt;
        }
        return StackPaint(std::move(layers));
    }

    // A pattern keyword may be followed by one number: its period. Every
    // other number is still a weight -- the one clause patterns cost the
    // grammar.
    int period = 1;
    if ((pattern || isBlur) && at < tokens.size() && tokens[at].kind == PaintToken::Kind::Number) {
        period = static_cast<int>(std::lround(tokens[at].number));
        if (period < 1) {
            Fail(error, "period must be at least 1");
            return std::nullopt;
        }
        ++at;
    }

    // --- stops, with weights between them -------------------------------
    std::vector<ColorStop> colours;
    std::vector<FadeStop>  fades;
    float                  pendingWeight = 1.0F;

    for (; at < tokens.size(); ++at) {
        const PaintToken& token = tokens[at];
        if (token.kind == PaintToken::Kind::Number) {
            pendingWeight = static_cast<float>(token.number);
            if (pendingWeight <= 0.0F) {
                Fail(error, "a weight must be positive");
                return std::nullopt;
            }
            continue;
        }
        if (token.kind == PaintToken::Kind::Nested) {
            Fail(error, "only :stack takes nested paints");
            return std::nullopt;
        }

        // A stop naming a paint expands to that paint's own stops, which is
        // what makes presets building blocks rather than terminal values.
        if (token.text.starts_with('$') && context.named) {
            if (const std::optional<Paint> referenced = context.named(std::string_view(token.text).substr(1))) {
                if (!referenced->fades.empty()) {
                    for (std::size_t i = 0; i < referenced->fades.size(); ++i) {
                        FadeStop stop = referenced->fades[i];
                        if (i == 0) {
                            stop.weight = pendingWeight;
                        }
                        fades.push_back(stop);
                    }
                }
                else {
                    for (std::size_t i = 0; i < referenced->stops.size(); ++i) {
                        ColorStop stop = referenced->stops[i];
                        if (i == 0) {
                            stop.weight = pendingWeight;
                        }
                        colours.push_back(stop);
                    }
                }
                pendingWeight = 1.0F;
                continue;
            }
        }

        if (const std::optional<double> amount = ParsePercentStop(token.text)) {
            fades.push_back(FadeStop{static_cast<float>(*amount), pendingWeight});
            pendingWeight = 1.0F;
            continue;
        }
        if (const std::optional<Color> colour = ParseColorStop(token.text, context)) {
            colours.push_back(ColorStop{*colour, pendingWeight});
            pendingWeight = 1.0F;
            continue;
        }
        Fail(error, "not a colour, a percentage, or a known $reference: " + token.text);
        return std::nullopt;
    }

    if (!colours.empty() && !fades.empty()) {
        Fail(error, "a paint is colours or percentages, never both");
        return std::nullopt;
    }

    if (isBlur) {
        const Color tint = colours.empty() ? Color::RGB(0x000000).WithAlpha(0) : colours.front().colour;
        return BlurPaint(period, tint);
    }
    if (pattern) {
        if (colours.empty()) {
            Fail(error, "a pattern needs at least one colour stop");
            return std::nullopt;
        }
        Paint paint = PatternPaint(*pattern, period, std::move(colours));
        paint.axis  = axis;
        return paint;
    }
    if (!fades.empty()) {
        return FadePaint(axis, std::move(fades));
    }
    if (colours.empty()) {
        Fail(error, "no stops");
        return std::nullopt;
    }
    return GradientPaint(axis, std::move(colours));
}

std::optional<Paint> ParsePaint(std::string_view line, const PaintContext& context, std::string* error) {
    std::vector<PaintToken> tokens;
    std::istringstream      stream{std::string(line)};
    std::string             word;
    while (stream >> word) {
        try {
            std::size_t  consumed = 0;
            const double value    = std::stod(word, &consumed);
            if (consumed == word.size()) {
                tokens.push_back(PaintToken::Num(value));
                continue;
            }
        }
        catch (const std::exception&) {
            // Not a number, so it is a stop or a keyword -- fall through.
        }
        tokens.push_back(PaintToken::Str(word));
    }
    return ParsePaint(tokens, context, error);
}

std::string PaintToString(const Paint& paint) {
    std::ostringstream out;

    auto writeWeight = [&out](float weight, bool first) {
        if (!first && weight != 1.0F) {
            out << ' ' << (weight == std::floor(weight) ? std::to_string(static_cast<long long>(weight)) : std::to_string(weight));
        }
    };

    switch (paint.kind) {
        case PaintKind::Stack:
            return {}; // no nesting in the one-line form, by design
        case PaintKind::Blur:
            out << "blur " << paint.period;
            if (!paint.stops.empty()) {
                out << ' ' << ColorToToken(paint.stops.front().colour);
            }
            return out.str();
        case PaintKind::Pattern:
            out << PatternName(paint.pattern) << ' ' << paint.period;
            for (std::size_t i = 0; i < paint.stops.size(); ++i) {
                writeWeight(paint.stops[i].weight, i == 0);
                out << ' ' << ColorToToken(paint.stops[i].colour);
            }
            return out.str();
        case PaintKind::Fade:
            out << AxisName(paint.axis);
            for (std::size_t i = 0; i < paint.fades.size(); ++i) {
                writeWeight(paint.fades[i].weight, i == 0);
                out << ' ' << static_cast<int>(std::lround(paint.fades[i].amount * 100.0)) << '%';
            }
            return out.str();
        case PaintKind::Solid:
            return paint.stops.empty() ? std::string{} : ColorToToken(paint.stops.front().colour);
        case PaintKind::Gradient:
        default:
            out << AxisName(paint.axis);
            for (std::size_t i = 0; i < paint.stops.size(); ++i) {
                writeWeight(paint.stops[i].weight, i == 0);
                out << ' ' << ColorToToken(paint.stops[i].colour);
            }
            return out.str();
    }
}

std::vector<std::string> ThemeSlots() {
    return {"bg", "fg", "subtle", "selection", "search", "accent", "desktop-accent", "border",
            "chrome-bg", "chrome-fg", "error", "warning", "info", "hint", "added", "removed",
            "comment", "string", "keyword", "number", "type", "function"};
}

} // namespace ned::ui
