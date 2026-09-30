#include "PrivateFile.h"

#include <cstdlib>
#include <system_error>
#include <vector>

#include <fcntl.h>
#include <unistd.h>

namespace ned::editor::tracker {

namespace {

    std::filesystem::path PrivateDirectory() {
        if (const char* runtime = std::getenv("XDG_RUNTIME_DIR"); runtime && *runtime) {
            std::error_code ec;
            if (std::filesystem::is_directory(runtime, ec)) {
                return runtime;
            }
        }
        return std::filesystem::temp_directory_path();
    }

} // namespace

std::expected<std::shared_ptr<const PrivateFile>, std::string> PrivateFile::Write(std::string_view content) {
    const std::string templatePath = (PrivateDirectory() / "ned-tracker-XXXXXX").string();
    std::vector<char> nameBuffer(templatePath.begin(), templatePath.end());
    nameBuffer.push_back('\0');

    // mkstemp creates the file 0600 and exclusively, so nothing else can
    // open it between its creation and the write.
    const int fd = ::mkostemp(nameBuffer.data(), O_CLOEXEC);
    if (fd == -1) {
        return std::unexpected("couldn't create a private file in " + templatePath.substr(0, templatePath.rfind('/')));
    }
    // Owned from here, so every failure below removes it.
    auto file = std::make_shared<const PrivateFile>(Key{}, std::filesystem::path(nameBuffer.data()));

    std::size_t written = 0;
    while (written < content.size()) {
        const ssize_t chunk = ::write(fd, content.data() + written, content.size() - written);
        if (chunk <= 0) {
            ::close(fd);
            return std::unexpected(std::string("couldn't write a private file"));
        }
        written += static_cast<std::size_t>(chunk);
    }
    if (::close(fd) != 0) {
        return std::unexpected(std::string("couldn't write a private file"));
    }
    return file;
}

PrivateFile::~PrivateFile() {
    std::error_code ignored;
    std::filesystem::remove(path_, ignored);
}

} // namespace ned::editor::tracker
