//
// Time on an issue through Org clocking: an Org heading carries the issue's
// key and connection as :ISSUE: and :TRACKER: properties, clocking in and
// out of it is ordinary Org, and a clock-out's interval becomes the
// tracker's worklog. Org clock reports then roll up by issue for free.
//

#ifndef NED_EDITOR_TRACKER_CLOCKING_H
#define NED_EDITOR_TRACKER_CLOCKING_H

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "Editor/Org.h"
#include "Provider.h"

namespace ned::text {
class Buffer;
} // namespace ned::text

namespace ned::editor::tracker {

inline constexpr std::string_view kIssueProperty   = "ISSUE";
inline constexpr std::string_view kTrackerProperty = "TRACKER";

// The Org file issue headings are made in: set by ned/set-tracker-clock-file,
// else $XDG_DATA_HOME/ned/issues.org.
void                                SetClockFile(std::filesystem::path path);
[[nodiscard]] std::filesystem::path ClockFile();

// The line start of the heading whose :ISSUE: is key (and whose :TRACKER:,
// if it has one, is connection).
[[nodiscard]] std::optional<std::size_t> FindIssueHeading(std::string_view bufferText, const std::string& key,
                                                          const std::string& connection);

// Finds key's heading in buffer, or appends "* KEY title" with its
// properties; its line start either way.
std::size_t EnsureIssueHeading(text::Buffer& buffer, const Issue& issue, const std::string& connection);

// A CLOCK: timestamp as seconds since the epoch (Org.h's ClockTimePoint).
[[nodiscard]] std::int64_t EpochSeconds(const org::OrgTimestamp& timestamp);

// The running clock, when it is on an issue heading: what its worklog
// would be if it stopped at now.
struct IssueClock {
    std::string key;
    std::string connection;
    Worklog     worklog;
};
[[nodiscard]] std::optional<IssueClock> RunningIssueClock(std::string_view                      bufferText,
                                                          std::chrono::system_clock::time_point now = std::chrono::system_clock::now());

// "1:25" for 85 minutes, the way Org writes a clock's duration.
[[nodiscard]] std::string ClockDuration(std::int64_t seconds);

} // namespace ned::editor::tracker

#endif // NED_EDITOR_TRACKER_CLOCKING_H
