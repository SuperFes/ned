//
// file: URIs (RFC 8089) for LSP, ACP and anything else that names a local
// file by URI. The path component is percent-encoded per RFC 3986.
//

#ifndef NED_TEXT_FILE_URI_H
#define NED_TEXT_FILE_URI_H

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace ned::text {

// "file://" + `path` with every byte outside the unreserved set and '/'
// percent-encoded. `path` is used as given; absolutize it first.
[[nodiscard]] std::string PathToFileUri(const std::filesystem::path& path);

// The percent-decoded path a file: URI names; nullopt for any other scheme.
// A '%' not followed by two hex digits is kept verbatim.
[[nodiscard]] std::optional<std::filesystem::path> FileUriToPath(std::string_view uri);

} // namespace ned::text

#endif // NED_TEXT_FILE_URI_H
