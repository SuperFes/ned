#include "FileIcons.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
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

    struct Kind {
        std::string_view text;
        std::uint32_t    rgb; // nvim-web-devicons' colour for the kind
    };

    // Kinds no language claims, by lowercased extension.
    const std::unordered_map<std::string, Kind>& KindByExtension() {
        static const std::unordered_map<std::string, Kind> table = [] {
            std::unordered_map<std::string, Kind> built;
            const auto                            add = [&](Kind kind, std::initializer_list<const char*> extensions) {
                for (const char* extension : extensions) {
                    built.emplace(extension, kind);
                }
            };
            // nf-seti-image
            add({"\ue60d", 0xa074c4}, {".png", ".jpg", ".jpeg", ".gif", ".bmp", ".webp", ".ico", ".tif", ".tiff", ".avif", ".heic", ".qoi"});
            add({"\ue698", 0xffb13b}, {".svg"}); // nf-seti-svg
            // nf-oct-file_zip
            add({"\uf410", 0xeca517}, {".zip", ".tar", ".gz", ".tgz", ".bz2", ".xz", ".zst", ".7z", ".rar", ".lz", ".lzma", ".jar", ".deb", ".rpm"});
            add({"\ue67d", 0xb30b00}, {".pdf"});                                                   // nf-seti-pdf
            add({"\ue659", 0xececec}, {".ttf", ".otf", ".woff", ".woff2", ".eot"});                // nf-seti-font
            add({"\ue638", 0x00afff}, {".mp3", ".wav", ".flac", ".ogg", ".opus", ".m4a", ".aac"}); // nf-seti-audio
            add({"\ue69f", 0xfd971f}, {".mp4", ".mkv", ".webm", ".mov", ".avi", ".m4v"});          // nf-seti-video
            add({"\ue64d", 0xdad8d8}, {".db", ".sqlite", ".sqlite3"});                             // nf-seti-db
            add({"\ue672", 0xbbbbbb}, {".lock"});                                                  // nf-seti-lock
            add({"\ue64a", 0x89e051}, {".csv", ".tsv"});                                           // nf-seti-csv
            // nf-oct-file_binary
            add({"\uf471", 0x9f0500}, {".o", ".obj", ".a", ".so", ".dylib", ".dll", ".exe", ".bin", ".wasm", ".class", ".pyc"});
            return built;
        }();
        return table;
    }

    Glyph KindIcon(const std::filesystem::path& path) {
        const std::string filename = path.filename().string();
        if (filename.starts_with("LICENSE") || filename.starts_with("LICENCE") || filename.starts_with("COPYING")) {
            return Glyph{.text = "\ue60a", .rgb = 0xd0bf41}; // nf-seti-license
        }
        std::string extension = path.extension().string();
        std::ranges::transform(extension, extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        const auto& table = KindByExtension();
        if (const auto it = table.find(extension); it != table.end()) {
            return Glyph{.text = std::string(it->second.text), .rgb = it->second.rgb};
        }
        return Glyph{.text = std::string(kDocument), .rgb = std::nullopt};
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

Glyph FileIcon(const std::filesystem::path& path) {
    if (!FileTypeIconsEnabled()) {
        return {};
    }
    Glyph icon = IconForPath(path);
    return icon.empty() ? KindIcon(path) : icon;
}

Glyph FileIcon(const text::Buffer& buffer) {
    if (!FileTypeIconsEnabled()) {
        return {};
    }
    if (Glyph icon = IconForBuffer(buffer); !icon.empty()) {
        return icon;
    }
    if (buffer.Path()) {
        return KindIcon(*buffer.Path());
    }
    if (buffer.Remote()) {
        return KindIcon(buffer.Remote()->path);
    }
    return Glyph{.text = std::string(kDocument), .rgb = std::nullopt};
}

Glyph FolderIcon(bool open) {
    if (!FileTypeIconsEnabled()) {
        return {};
    }
    return Glyph{.text = std::string(open ? kFolderOpen : kFolder), .rgb = std::nullopt};
}

} // namespace ned::editor
