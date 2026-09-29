//
// Saving a picture the way it arrived -- its own bytes, not re-encoded --
// into the user's download directory.
//

#ifndef NED_EDITOR_IMAGE_SAVE_H
#define NED_EDITOR_IMAGE_SAVE_H

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace ned::editor::image {

// The directory `key` (XDG_DOWNLOAD_DIR, ...) names in the text of a
// user-dirs.dirs file, with a leading $HOME expanded; nullopt when it's
// absent or isn't the absolute or $HOME-relative path the format allows.
[[nodiscard]] std::optional<std::filesystem::path> UserDirFromConfig(std::string_view config, std::string_view key,
                                                                     const std::filesystem::path& home);

// Where saved pictures go: $XDG_DOWNLOAD_DIR, else the download directory
// $XDG_CONFIG_HOME/user-dirs.dirs names, else ~/Downloads. nullopt without
// a HOME to fall back on.
[[nodiscard]] std::optional<std::filesystem::path> DownloadDirectory();

// ".png", ".jpg", ... for a picture: from its MIME type, else its leading
// bytes, else ".bin".
[[nodiscard]] std::string ImageFileExtension(std::string_view mimeType, std::string_view bytes);

struct WrittenFile {
    std::filesystem::path path;  // empty on failure
    std::string           error; // why, on failure
};

// Writes `bytes` to a new file `stem` + `extension` in `directory`
// (created if missing), naming it stem-2, stem-3, ... rather than replace
// a file already there.
[[nodiscard]] WrittenFile WriteNewFile(const std::filesystem::path& directory, std::string_view stem, std::string_view extension,
                                       std::string_view bytes);

} // namespace ned::editor::image

#endif // NED_EDITOR_IMAGE_SAVE_H
