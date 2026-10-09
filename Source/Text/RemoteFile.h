//
// A file on a remote host, written destination:/absolute/path the way scp
// and the mode line spell it. A destination on a port other than 22 can
// only be an ssh URI, ssh://[user@]host:port, so a file there is written as
// one: ssh://[user@]host:port/absolute/path, as scp takes it too. A buffer
// holding one has no local path, so nothing that reads or writes a buffer's
// local file ever touches a same-named file here.
//

#ifndef NED_TEXT_REMOTEFILE_H
#define NED_TEXT_REMOTEFILE_H

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace ned::text {

// The scheme a destination with a port starts with.
inline constexpr std::string_view kSshScheme = "ssh://";

struct RemoteFile {
    // What ssh is given: [user@]host, or ssh://[user@]host:port for any
    // port but 22 (NormalizeDestination).
    std::string           destination;
    std::filesystem::path path; // absolute, lexically normal, no trailing separator

    [[nodiscard]] std::string ToString() const;
    [[nodiscard]] bool        operator==(const RemoteFile&) const = default;
};

// A file here, by its path, or one on a host.
using FileRef = std::variant<std::filesystem::path, RemoteFile>;

// Its path on whichever machine holds it: for naming it, not opening it.
[[nodiscard]] const std::filesystem::path& PathOf(const FileRef& file);

// The path, or destination:/path.
[[nodiscard]] std::string ToString(const FileRef& file);

// nullopt unless text is a destination, ':', then an absolute path. The
// first ":/" splits them, so a bracketed IPv6 destination parses too. An
// ssh URI's path follows its authority, with or without a ':' between.
[[nodiscard]] std::optional<RemoteFile> ParseRemoteFile(std::string_view text);

// One spelling per destination: an ssh URI on port 22, or with none, is
// [user@]host; any other destination is returned as given.
[[nodiscard]] std::string NormalizeDestination(std::string_view destination);

// A destination short enough for a tab: no user, and a host name without
// its domain, then any port. An address stays whole.
[[nodiscard]] std::string ShortDestination(std::string_view destination);

// What a buffer holding file is called: "host:name", so it reads apart
// from a local file of the same name wherever buffer names show.
[[nodiscard]] std::string RemoteBufferName(const RemoteFile& file);

} // namespace ned::text

#endif // NED_TEXT_REMOTEFILE_H
