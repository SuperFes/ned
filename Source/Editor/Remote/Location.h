//
// A place on a remote host, written destination:/absolute/path the way scp
// and the mode line spell it.
//

#ifndef NED_EDITOR_REMOTE_LOCATION_H
#define NED_EDITOR_REMOTE_LOCATION_H

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace ned::editor::remote {

struct Location {
    std::string           destination;
    std::filesystem::path path; // absolute, lexically normal, no trailing separator

    [[nodiscard]] std::string ToString() const;
};

// nullopt unless text is a destination, ':', then an absolute path. The
// first ":/" splits them, so a bracketed IPv6 destination parses too.
[[nodiscard]] std::optional<Location> ParseLocation(std::string_view text);

} // namespace ned::editor::remote

#endif // NED_EDITOR_REMOTE_LOCATION_H
