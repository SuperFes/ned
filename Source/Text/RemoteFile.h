//
// A file on a remote host, written destination:/absolute/path the way scp
// and the mode line spell it. A buffer holding one has no local path, so
// nothing that reads or writes a buffer's local file ever touches a
// same-named file here.
//

#ifndef NED_TEXT_REMOTEFILE_H
#define NED_TEXT_REMOTEFILE_H

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace ned::text {

struct RemoteFile {
    std::string           destination;
    std::filesystem::path path; // absolute, lexically normal, no trailing separator

    [[nodiscard]] std::string ToString() const;
    [[nodiscard]] bool        operator==(const RemoteFile&) const = default;
};

// nullopt unless text is a destination, ':', then an absolute path. The
// first ":/" splits them, so a bracketed IPv6 destination parses too.
[[nodiscard]] std::optional<RemoteFile> ParseRemoteFile(std::string_view text);

// A destination short enough for a tab: no user, and a host name without
// its domain. An address stays whole.
[[nodiscard]] std::string ShortDestination(std::string_view destination);

// What a buffer holding file is called: "host:name", so it reads apart
// from a local file of the same name wherever buffer names show.
[[nodiscard]] std::string RemoteBufferName(const RemoteFile& file);

} // namespace ned::text

#endif // NED_TEXT_REMOTEFILE_H
