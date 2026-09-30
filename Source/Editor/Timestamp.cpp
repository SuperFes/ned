#include "Timestamp.h"

#include <ctime>

namespace ned::editor {

namespace {

    // Exactly `digits` decimal digits at text[at].
    std::optional<int> Digits(std::string_view text, std::size_t at, std::size_t digits) {
        if (at + digits > text.size()) {
            return std::nullopt;
        }
        int value = 0;
        for (std::size_t i = at; i < at + digits; ++i) {
            if (text[i] < '0' || text[i] > '9') {
                return std::nullopt;
            }
            value = value * 10 + (text[i] - '0');
        }
        return value;
    }

} // namespace

std::optional<std::int64_t> ParseIso8601(std::string_view text) {
    // YYYY-MM-DDTHH:MM:SS is 19 characters, separators at fixed positions.
    if (text.size() < 19 || text[4] != '-' || text[7] != '-' || (text[10] != 'T' && text[10] != ' ') || text[13] != ':' ||
        text[16] != ':') {
        return std::nullopt;
    }
    const auto year   = Digits(text, 0, 4);
    const auto month  = Digits(text, 5, 2);
    const auto day    = Digits(text, 8, 2);
    const auto hour   = Digits(text, 11, 2);
    const auto minute = Digits(text, 14, 2);
    const auto second = Digits(text, 17, 2);
    if (!year || !month || !day || !hour || !minute || !second || *month < 1 || *month > 12 || *day < 1 || *day > 31) {
        return std::nullopt;
    }

    std::size_t at = 19;
    if (at < text.size() && text[at] == '.') {
        ++at;
        while (at < text.size() && text[at] >= '0' && text[at] <= '9') {
            ++at;
        }
    }
    std::int64_t offset = 0;
    if (at < text.size()) {
        if (text[at] == 'Z') {
            ++at;
        }
        else if (text[at] == '+' || text[at] == '-') {
            const int  sign        = text[at] == '-' ? -1 : 1;
            const bool colon       = at + 3 < text.size() && text[at + 3] == ':';
            const auto offsetHours = Digits(text, at + 1, 2);
            const auto offsetMins  = Digits(text, at + (colon ? 4 : 3), 2);
            if (!offsetHours || !offsetMins) {
                return std::nullopt;
            }
            offset = sign * (static_cast<std::int64_t>(*offsetHours) * 3600 + static_cast<std::int64_t>(*offsetMins) * 60);
            at += colon ? 6 : 5;
        }
    }
    if (at != text.size()) {
        return std::nullopt;
    }

    std::tm utc{};
    utc.tm_year = *year - 1900;
    utc.tm_mon  = *month - 1;
    utc.tm_mday = *day;
    utc.tm_hour = *hour;
    utc.tm_min  = *minute;
    utc.tm_sec  = *second;
    // A local time at +02:00 is two hours ahead of UTC.
    return static_cast<std::int64_t>(timegm(&utc)) - offset;
}

std::string CompactAge(std::int64_t seconds) {
    constexpr std::int64_t kMinute = 60;
    constexpr std::int64_t kHour   = 60 * kMinute;
    constexpr std::int64_t kDay    = 24 * kHour;
    constexpr std::int64_t kWeek   = 7 * kDay;
    constexpr std::int64_t kMonth  = 30 * kDay;
    constexpr std::int64_t kYear   = 365 * kDay;
    if (seconds < kMinute) {
        return "now";
    }
    if (seconds < kHour) {
        return std::to_string(seconds / kMinute) + "m";
    }
    if (seconds < kDay) {
        return std::to_string(seconds / kHour) + "h";
    }
    if (seconds < kWeek) {
        return std::to_string(seconds / kDay) + "d";
    }
    if (seconds < 2 * kMonth) {
        return std::to_string(seconds / kWeek) + "w";
    }
    if (seconds < kYear) {
        return std::to_string(seconds / kMonth) + "mo";
    }
    return std::to_string(seconds / kYear) + "y";
}

} // namespace ned::editor
