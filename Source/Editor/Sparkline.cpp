#include "Sparkline.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <vector>

namespace ned::editor {

namespace {

    // U+2581 LOWER ONE EIGHTH BLOCK .. U+2588 FULL BLOCK -- 8 height levels,
    // each a 3-byte UTF-8 sequence (both codepoints sit in the 0x2580 block).
    constexpr const char* kBlockGlyphs[8] = {
        "▁",
        "▂",
        "▃",
        "▄",
        "▅",
        "▆",
        "▇",
        "█",
    };

} // namespace

bool TryParseNumeric(std::string_view text, double& out) {
    std::size_t begin = 0;
    std::size_t end   = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin]))) {
        ++begin;
    }
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }
    if (begin >= end) {
        return false;
    }
    const std::string_view trimmed = text.substr(begin, end - begin);
    double                 value   = 0.0;
    const auto             result  = std::from_chars(trimmed.data(), trimmed.data() + trimmed.size(), value);
    // Require the whole trimmed text to parse as the number -- "42abc"/
    // "1, 2" must not be accepted just because a prefix of them is numeric.
    if (result.ec != std::errc{} || result.ptr != trimmed.data() + trimmed.size()) {
        return false;
    }
    out = value;
    return true;
}

std::string BuildBlockSparkline(std::span<const double> values, std::size_t maxWidth) {
    if (values.empty() || maxWidth == 0) {
        return {};
    }

    std::vector<double> samples;
    if (values.size() <= maxWidth) {
        samples.assign(values.begin(), values.end());
    }
    else {
        // Bucket-average downsample: values.size()/maxWidth per bucket, the
        // remainder spread across the trailing buckets so every input value
        // lands in exactly one bucket and no bucket is ever empty.
        samples.reserve(maxWidth);
        const std::size_t base  = values.size() / maxWidth;
        const std::size_t extra = values.size() % maxWidth;
        std::size_t       index = 0;
        for (std::size_t bucket = 0; bucket < maxWidth; ++bucket) {
            const std::size_t count = base + (bucket < extra ? 1 : 0);
            double            sum   = 0.0;
            for (std::size_t i = 0; i < count; ++i) {
                sum += values[index++];
            }
            samples.push_back(sum / static_cast<double>(count));
        }
    }

    const auto [minIt, maxIt] = std::minmax_element(samples.begin(), samples.end());
    const double minValue     = *minIt;
    const double range        = *maxIt - minValue;

    std::string result;
    result.reserve(samples.size() * 3); // each glyph is a 3-byte UTF-8 sequence
    for (const double value : samples) {
        std::size_t level;
        if (range <= 0.0) {
            level = 3; // flat series -- a steady mid-height line, not silence
        }
        else {
            const double normalized = (value - minValue) / range;
            level                   = static_cast<std::size_t>(std::clamp(normalized * 7.0 + 0.5, 0.0, 7.0));
        }
        result += kBlockGlyphs[level];
    }
    return result;
}

} // namespace ned::editor
