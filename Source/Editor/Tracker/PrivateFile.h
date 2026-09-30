//
// A temporary file only the user can read, removed when its last owner lets
// go: a token's curl config, a comment body a command reads from a path.
//

#ifndef NED_EDITOR_TRACKER_PRIVATEFILE_H
#define NED_EDITOR_TRACKER_PRIVATEFILE_H

#include <expected>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace ned::editor::tracker {

class PrivateFile {
  public:
    // A mkstemp'd 0600 file in $XDG_RUNTIME_DIR (a per-user 0700 tmpfs)
    // when set, else the temp dir.
    [[nodiscard]] static std::expected<std::shared_ptr<const PrivateFile>, std::string> Write(std::string_view content);

    struct Key {
        explicit Key() = default;
    };
    PrivateFile(Key, std::filesystem::path path) : path_(std::move(path)) {
    }
    ~PrivateFile();

    PrivateFile(const PrivateFile&)            = delete;
    PrivateFile& operator=(const PrivateFile&) = delete;

    [[nodiscard]] const std::filesystem::path& Path() const {
        return path_;
    }

  private:
    std::filesystem::path path_;
};

} // namespace ned::editor::tracker

#endif // NED_EDITOR_TRACKER_PRIVATEFILE_H
