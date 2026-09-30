#include "Clocking.h"

#include <cstdlib>
#include <mutex>
#include <stdexcept>
#include <utility>

#include "Text/Buffer.h"

namespace ned::editor::tracker {

namespace {

    struct ClockFileSetting {
        std::mutex                           mutex;
        std::optional<std::filesystem::path> path;
    };

    ClockFileSetting& Setting() {
        static ClockFileSetting setting;
        return setting;
    }

} // namespace

void SetClockFile(std::filesystem::path path) {
    ClockFileSetting&     setting = Setting();
    const std::lock_guard lock(setting.mutex);
    setting.path = path.empty() ? std::nullopt : std::optional(std::move(path));
}

std::filesystem::path ClockFile() {
    {
        ClockFileSetting&     setting = Setting();
        const std::lock_guard lock(setting.mutex);
        if (setting.path) {
            return *setting.path;
        }
    }
    if (const char* xdgDataHome = std::getenv("XDG_DATA_HOME"); xdgDataHome && *xdgDataHome) {
        return std::filesystem::path(xdgDataHome) / "ned" / "issues.org";
    }
    if (const char* home = std::getenv("HOME"); home && *home) {
        return std::filesystem::path(home) / ".local" / "share" / "ned" / "issues.org";
    }
    throw std::runtime_error("ned: cannot determine data directory (neither XDG_DATA_HOME nor HOME is set)");
}

std::optional<std::size_t> FindIssueHeading(std::string_view bufferText, const std::string& key, const std::string& connection) {
    for (const org::Headline& headline : org::ParseOutline(bufferText)) {
        if (org::GetProperty(bufferText, headline, kIssueProperty) != key) {
            continue;
        }
        const std::optional<std::string> tracker = org::GetProperty(bufferText, headline, kTrackerProperty);
        if (!tracker || *tracker == connection) {
            return headline.lineStartByte;
        }
    }
    return std::nullopt;
}

std::size_t EnsureIssueHeading(text::Buffer& buffer, const Issue& issue, const std::string& connection) {
    const std::string text = buffer.Text();
    if (const auto found = FindIssueHeading(text, issue.key, connection)) {
        return *found;
    }
    std::string heading = "* " + issue.key + (issue.title.empty() ? "" : " " + issue.title) + "\n:PROPERTIES:\n:" +
                          std::string(kIssueProperty) + ": " + issue.key + "\n:" + std::string(kTrackerProperty) + ": " + connection +
                          "\n:END:\n";
    if (!text.empty() && !text.ends_with('\n')) {
        heading.insert(heading.begin(), '\n');
    }
    const std::size_t at = text.size() + (heading.front() == '\n' ? 1 : 0);
    buffer.SetPoint(text.size());
    buffer.InsertAtPoint(heading);
    return at;
}

std::int64_t EpochSeconds(const org::OrgTimestamp& timestamp) {
    return std::chrono::duration_cast<std::chrono::seconds>(org::ClockTimePoint(timestamp).time_since_epoch()).count();
}

std::optional<IssueClock> RunningIssueClock(std::string_view bufferText, std::chrono::system_clock::time_point now) {
    const std::optional<org::RunningClock> running = org::CurrentlyRunningClock(bufferText);
    if (!running) {
        return std::nullopt;
    }
    const std::optional<std::string> key = org::GetProperty(bufferText, running->headline, kIssueProperty);
    if (!key || key->empty()) {
        return std::nullopt;
    }
    return IssueClock{
        .key        = *key,
        .connection = org::GetProperty(bufferText, running->headline, kTrackerProperty).value_or(""),
        .worklog    = Worklog{.started = EpochSeconds(running->start),
                              .seconds = static_cast<std::int64_t>(org::ElapsedMinutes(running->start, now).count()) * 60},
    };
}

std::string ClockDuration(std::int64_t seconds) {
    const std::int64_t minutes = seconds / 60;
    const std::int64_t rest    = minutes % 60;
    return std::to_string(minutes / 60) + ":" + (rest < 10 ? "0" : "") + std::to_string(rest);
}

} // namespace ned::editor::tracker
