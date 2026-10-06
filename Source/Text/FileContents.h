//
// A file's bytes as read for a load, apart from decoding them
// (Buffer::FromContents), so a file loads the same way whether it was read
// here or on a remote host.
//

#ifndef NED_TEXT_FILECONTENTS_H
#define NED_TEXT_FILECONTENTS_H

#include <filesystem>
#include <optional>
#include <string>

namespace ned::text {

struct FileContents {
    std::string bytes;
    // Taken before the read, so a write landing during it shows as a later
    // external change rather than being absorbed.
    std::optional<std::filesystem::file_time_type> timestamp;
};

// Throws std::runtime_error when the file can't be opened or read.
[[nodiscard]] FileContents ReadFileContents(const std::filesystem::path& path);

} // namespace ned::text

#endif // NED_TEXT_FILECONTENTS_H
