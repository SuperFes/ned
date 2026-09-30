//
// ISO 8601 timestamps as external services send them, and the compact
// relative age a narrow column shows for one.
//

#ifndef NED_EDITOR_TIMESTAMP_H
#define NED_EDITOR_TIMESTAMP_H

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace ned::editor {

// Seconds since the epoch for "YYYY-MM-DDTHH:MM:SS", optionally with
// fractional seconds and a "Z", "+HH:MM" or "+HHMM" offset (none is UTC):
// GitHub's "2026-09-29T15:05:44Z", Jira's "2026-09-29T15:05:44.175+0200".
[[nodiscard]] std::optional<std::int64_t> ParseIso8601(std::string_view text);

// "now", "42m", "5h", "3d", "6w", "4mo", "2y" -- the largest whole unit. A
// negative age (clock skew) reads as "now".
[[nodiscard]] std::string CompactAge(std::int64_t seconds);

} // namespace ned::editor

#endif // NED_EDITOR_TIMESTAMP_H
