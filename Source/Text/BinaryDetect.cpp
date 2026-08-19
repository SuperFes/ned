#include "BinaryDetect.h"

#include <algorithm>
#include <array>
#include <fstream>

namespace ned::text {

bool LooksBinary(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return true; // unreadable -- not worth treating as text either
    }

    std::array<char, 8192> buffer{};
    file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const auto bytesRead = static_cast<std::size_t>(file.gcount());
    return std::find(buffer.data(), buffer.data() + bytesRead, '\0') != buffer.data() + bytesRead;
}

} // namespace ned::text
