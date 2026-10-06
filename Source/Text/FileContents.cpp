#include "FileContents.h"

#include <fstream>
#include <stdexcept>
#include <system_error>

namespace ned::text {

FileContents ReadFileContents(const std::filesystem::path& path) {
    FileContents    contents;
    std::error_code timestampError;
    const auto      timestamp = std::filesystem::last_write_time(path, timestampError);
    if (!timestampError) {
        contents.timestamp = timestamp;
    }

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("ned: cannot open file for reading: " + path.string());
    }
    std::uintmax_t size = 0;
    try {
        size = std::filesystem::file_size(path);
    }
    catch (const std::filesystem::filesystem_error&) {
        throw std::runtime_error("ned: cannot determine size of file: " + path.string());
    }
    contents.bytes.resize(static_cast<std::size_t>(size));
    file.read(contents.bytes.data(), static_cast<std::streamsize>(size));
    if (file.bad()) {
        throw std::runtime_error("ned: error reading file: " + path.string());
    }
    // A file that shrank since the size was taken reads short.
    contents.bytes.resize(static_cast<std::size_t>(file.gcount()));
    return contents;
}

} // namespace ned::text
