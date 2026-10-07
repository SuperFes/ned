#include "FileIcons.h"

#include <algorithm>
#include <cctype>
#include <mutex>
#include <string_view>
#include <unordered_map>

#include "ModeOverrides.h"
#include "Text/Buffer.h"

namespace ned::editor {

namespace {

    std::mutex& SettingsMutex() {
        static std::mutex mutex;
        return mutex;
    }

    bool& FileTypeIconsStorage() {
        static bool enabled = true;
        return enabled;
    }

    constexpr std::string_view kDocument   = "\uf016"; // nf-fa-file_o
    constexpr std::string_view kFolder     = "\uf07b"; // nf-fa-folder, as the dock rail
    constexpr std::string_view kFolderOpen = "\uf07c"; // nf-fa-folder_open

    // Kinds no language claims, by lowercased extension.
    const std::unordered_map<std::string, std::string_view>& KindByExtension() {
        static const std::unordered_map<std::string, std::string_view> table = [] {
            std::unordered_map<std::string, std::string_view> built;
            const auto                                        add = [&](std::string_view glyph, std::initializer_list<const char*> extensions) {
                for (const char* extension : extensions) {
                    built.emplace(extension, glyph);
                }
            };
            add("\ue60d", {".png", ".jpg", ".jpeg", ".gif", ".bmp", ".webp", ".ico", ".tif", ".tiff", ".avif", ".heic", ".qoi"});         // nf-seti-image
            add("\ue698", {".svg"});                                                                                                      // nf-seti-svg
            add("\uf410", {".zip", ".tar", ".gz", ".tgz", ".bz2", ".xz", ".zst", ".7z", ".rar", ".lz", ".lzma", ".jar", ".deb", ".rpm"}); // nf-oct-file_zip
            add("\ue67d", {".pdf"});                                                                                                      // nf-seti-pdf
            add("\ue659", {".ttf", ".otf", ".woff", ".woff2", ".eot"});                                                                   // nf-seti-font
            add("\ue638", {".mp3", ".wav", ".flac", ".ogg", ".opus", ".m4a", ".aac"});                                                    // nf-seti-audio
            add("\ue69f", {".mp4", ".mkv", ".webm", ".mov", ".avi", ".m4v"});                                                             // nf-seti-video
            add("\ue64d", {".db", ".sqlite", ".sqlite3"});                                                                                // nf-seti-db
            add("\ue672", {".lock"});                                                                                                     // nf-seti-lock
            add("\ue64a", {".csv", ".tsv"});                                                                                              // nf-seti-csv
            add("\uf471", {".o", ".obj", ".a", ".so", ".dylib", ".dll", ".exe", ".bin", ".wasm", ".class", ".pyc"});                      // nf-oct-file_binary
            return built;
        }();
        return table;
    }

    std::string_view KindIcon(const std::filesystem::path& path) {
        const std::string filename = path.filename().string();
        if (filename.starts_with("LICENSE") || filename.starts_with("LICENCE") || filename.starts_with("COPYING")) {
            return "\ue60a"; // nf-seti-license
        }
        std::string extension = path.extension().string();
        std::ranges::transform(extension, extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        const auto& table = KindByExtension();
        const auto  it    = table.find(extension);
        return it != table.end() ? it->second : kDocument;
    }

} // namespace

void SetFileTypeIconsEnabled(bool enabled) {
    const std::lock_guard<std::mutex> lock(SettingsMutex());
    FileTypeIconsStorage() = enabled;
}

bool FileTypeIconsEnabled() {
    const std::lock_guard<std::mutex> lock(SettingsMutex());
    return FileTypeIconsStorage();
}

std::string FileIcon(const std::filesystem::path& path) {
    if (!FileTypeIconsEnabled()) {
        return {};
    }
    std::string icon = IconForPath(path);
    return icon.empty() ? std::string(KindIcon(path)) : icon;
}

std::string FileIcon(const text::Buffer& buffer) {
    if (!FileTypeIconsEnabled()) {
        return {};
    }
    if (std::string icon = IconForBuffer(buffer); !icon.empty()) {
        return icon;
    }
    if (buffer.Path()) {
        return std::string(KindIcon(*buffer.Path()));
    }
    if (buffer.Remote()) {
        return std::string(KindIcon(buffer.Remote()->path));
    }
    return std::string(kDocument);
}

std::string FolderIcon(bool open) {
    return FileTypeIconsEnabled() ? std::string(open ? kFolderOpen : kFolder) : std::string();
}

} // namespace ned::editor
