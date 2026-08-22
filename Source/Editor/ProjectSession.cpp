#include "ProjectSession.h"

#include <cstdint>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <system_error>

#include <nlohmann/json.hpp>

#include "Session.h"

namespace ned::editor {

namespace {

    using Json = nlohmann::json;

    // The VCS markers DetectProjectRoot already recognizes (ProjectRoot.cpp),
    // plus this slice's own `.ned/` opt-in directory. Duplicated rather than
    // shared with ProjectRoot.cpp's private list, the same "not worth a new
    // dependency for something this small" call ProjectTree.cpp made for
    // IsDotDirectory.
    constexpr const char* kProjectMarkers[] = {".git", ".hg", ".svn", ".bzr", ".ned"};

    std::string Fnv1a64Hex(std::string_view key) {
        std::uint64_t hash = 1469598103934665603ull;
        for (const unsigned char byte : key) {
            hash ^= byte;
            hash *= 1099511628211ull;
        }
        char buffer[17];
        std::snprintf(buffer, sizeof(buffer), "%016llx", static_cast<unsigned long long>(hash));
        return buffer;
    }

    std::mutex& ProjectSessionMutex() {
        static std::mutex mutex;
        return mutex;
    }

    bool& RestoreEnabledStorage() {
        static bool enabled = true;
        return enabled;
    }

    std::optional<std::filesystem::path>& ActiveRootStorage() {
        static std::optional<std::filesystem::path> root;
        return root;
    }

    // What this process last wrote for the active root -- the dirty-skip
    // memo SaveActiveProjectSession compares against (see its doc comment).
    std::string& LastSavedJsonStorage() {
        static std::string lastSaved;
        return lastSaved;
    }

} // namespace

std::string ProjectSessionToJson(const ProjectSessionData& data, const std::filesystem::path& root) {
    Json openFiles = Json::array();
    for (const auto& file : data.openFiles) {
        openFiles.push_back(file.string());
    }

    Json breakpoints = Json::object();
    for (const auto& [pathKey, lines] : data.breakpoints) {
        breakpoints[pathKey] = lines;
    }

    Json windowLayout = Json::array();
    for (const WindowLayoutNode& node : data.windowLayout) {
        Json entry;
        switch (node.kind) {
            case WindowLayoutNode::Kind::Leaf:
                entry["kind"] = "leaf";
                break;
            case WindowLayoutNode::Kind::SplitBelow:
                entry["kind"] = "below";
                break;
            case WindowLayoutNode::Kind::SplitRight:
                entry["kind"] = "right";
                break;
        }
        if (node.file) {
            entry["file"] = node.file->string();
        }
        if (node.first) {
            entry["first"] = *node.first;
        }
        if (node.second) {
            entry["second"] = *node.second;
        }
        windowLayout.push_back(std::move(entry));
    }

    Json json = {
        {"version", 1},
        // Purely informational (which project a hashed XDG filename belongs
        // to, for a human poking at the directory) -- FromJson ignores it.
        {"root", root.string()},
        {"openFiles", std::move(openFiles)},
        {"breakpoints", std::move(breakpoints)},
    };
    if (data.activeFile) {
        json["activeFile"] = data.activeFile->string();
    }
    if (data.sidebarVisible) {
        json["sidebarVisible"] = *data.sidebarVisible;
    }
    if (data.sidebarWidth) {
        json["sidebarWidth"] = *data.sidebarWidth;
    }
    if (!windowLayout.empty()) {
        json["windowLayout"] = std::move(windowLayout);
    }
    if (!data.focusedPanePath.empty()) {
        json["focusedPanePath"] = data.focusedPanePath;
    }
    return json.dump(2);
}

std::optional<ProjectSessionData> ProjectSessionFromJson(std::string_view json) {
    try {
        const Json         parsed = Json::parse(json);
        ProjectSessionData data;

        for (const Json& file : parsed.value("openFiles", Json::array())) {
            if (file.is_string()) {
                data.openFiles.emplace_back(file.get<std::string>());
            }
        }
        if (parsed.contains("activeFile") && parsed["activeFile"].is_string()) {
            data.activeFile = std::filesystem::path(parsed["activeFile"].get<std::string>());
        }
        if (parsed.contains("sidebarVisible") && parsed["sidebarVisible"].is_boolean()) {
            data.sidebarVisible = parsed["sidebarVisible"].get<bool>();
        }
        if (parsed.contains("sidebarWidth") && parsed["sidebarWidth"].is_number_integer()) {
            data.sidebarWidth = parsed["sidebarWidth"].get<int>();
        }
        if (parsed.contains("breakpoints") && parsed["breakpoints"].is_object()) {
            for (const auto& [pathKey, lines] : parsed["breakpoints"].items()) {
                if (!lines.is_array()) {
                    continue; // one malformed entry shouldn't discard the rest
                }
                std::vector<std::size_t> parsedLines;
                for (const Json& line : lines) {
                    if (line.is_number_unsigned()) {
                        parsedLines.push_back(line.get<std::size_t>());
                    }
                }
                data.breakpoints.emplace(pathKey, std::move(parsedLines));
            }
        }
        if (parsed.contains("windowLayout") && parsed["windowLayout"].is_array()) {
            bool valid = true;
            for (const Json& entry : parsed["windowLayout"]) {
                if (!entry.is_object() || !entry.contains("kind") || !entry["kind"].is_string()) {
                    valid = false;
                    break;
                }
                WindowLayoutNode node;
                const std::string kind = entry["kind"].get<std::string>();
                if (kind == "leaf") {
                    node.kind = WindowLayoutNode::Kind::Leaf;
                    if (!entry.contains("file") || !entry["file"].is_string()) {
                        valid = false;
                        break;
                    }
                    node.file = std::filesystem::path(entry["file"].get<std::string>());
                }
                else if (kind == "below" || kind == "right") {
                    node.kind = kind == "below" ? WindowLayoutNode::Kind::SplitBelow : WindowLayoutNode::Kind::SplitRight;
                    if (!entry.contains("first") || !entry["first"].is_number_unsigned() || !entry.contains("second") ||
                        !entry["second"].is_number_unsigned()) {
                        valid = false;
                        break;
                    }
                    // Guards against a corrupted/malformed file's forward or
                    // self-referencing index recursing forever on restore --
                    // see WindowLayoutNode's own doc comment on the
                    // strictly-backward-pointing, post-order invariant.
                    const auto first  = entry["first"].get<std::size_t>();
                    const auto second = entry["second"].get<std::size_t>();
                    if (first >= data.windowLayout.size() || second >= data.windowLayout.size()) {
                        valid = false;
                        break;
                    }
                    node.first  = first;
                    node.second = second;
                }
                else {
                    valid = false;
                    break;
                }
                data.windowLayout.push_back(std::move(node));
            }
            if (!valid) {
                // A whole malformed tree, not one skippable entry (unlike
                // breakpoints above) -- indices are only meaningful relative
                // to a fully-intact vector, so falls back to no persisted
                // layout rather than restoring a partial/corrupt one.
                data.windowLayout.clear();
            }
        }
        if (parsed.contains("focusedPanePath") && parsed["focusedPanePath"].is_array()) {
            for (const Json& choice : parsed["focusedPanePath"]) {
                if (choice.is_number_integer()) {
                    data.focusedPanePath.push_back(choice.get<int>());
                }
            }
        }
        return data;
    }
    catch (const std::exception&) {
        return std::nullopt; // malformed -> no session, by contract
    }
}

bool HasProjectMarker(const std::filesystem::path& dir) {
    for (const char* marker : kProjectMarkers) {
        std::error_code ec;
        if (std::filesystem::is_directory(dir / marker, ec)) {
            return true;
        }
    }
    return false;
}

std::optional<std::filesystem::path> FindProjectMarkerRoot(const std::filesystem::path& startDir) {
    std::error_code       ec;
    std::filesystem::path current = std::filesystem::absolute(startDir, ec);
    if (ec) {
        return std::nullopt;
    }
    while (true) {
        if (HasProjectMarker(current)) {
            return current;
        }
        std::filesystem::path parent = current.parent_path();
        if (parent == current) {
            return std::nullopt; // filesystem root reached
        }
        current = std::move(parent);
    }
}

std::filesystem::path SessionsDirectory() {
    if (const char* xdgStateHome = std::getenv("XDG_STATE_HOME"); xdgStateHome && *xdgStateHome) {
        return std::filesystem::path(xdgStateHome) / "ned" / "sessions";
    }

    if (const char* home = std::getenv("HOME"); home && *home) {
        return std::filesystem::path(home) / ".local" / "state" / "ned" / "sessions";
    }

    throw std::runtime_error("ned: cannot determine state directory (neither XDG_STATE_HOME nor HOME is set)");
}

std::filesystem::path ProjectSessionPath(const std::filesystem::path& root) {
    std::error_code             ec;
    const std::filesystem::path optIn = root / ".ned";
    if (std::filesystem::is_directory(optIn, ec)) {
        return optIn / "session.json";
    }
    return SessionsDirectory() / (Fnv1a64Hex(FilePlaceStore::NormalizePathKey(root)) + ".json");
}

std::optional<ProjectSessionData> LoadProjectSessionFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    const std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return ProjectSessionFromJson(content);
}

void SaveProjectSessionFile(const ProjectSessionData& data, const std::filesystem::path& root,
                            const std::filesystem::path& path) {
    std::filesystem::create_directories(path.parent_path());

    const std::filesystem::path temporary = path.string() + ".ned-tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) {
            throw std::runtime_error("ned: cannot write project session to \"" + temporary.string() + "\"");
        }
        file << ProjectSessionToJson(data, root);
        if (!file.flush()) {
            throw std::runtime_error("ned: failed writing project session to \"" + temporary.string() + "\"");
        }
    }
    std::filesystem::rename(temporary, path);
}

// -- Process-wide root + toggle -----------------------------------------------

void SetSessionRestoreEnabled(bool enabled) {
    const std::lock_guard<std::mutex> lock(ProjectSessionMutex());
    RestoreEnabledStorage() = enabled;
}

bool SessionRestoreEnabled() {
    const std::lock_guard<std::mutex> lock(ProjectSessionMutex());
    return RestoreEnabledStorage();
}

void SetActiveProjectSessionRoot(std::optional<std::filesystem::path> root) {
    const std::lock_guard<std::mutex> lock(ProjectSessionMutex());
    ActiveRootStorage() = std::move(root);
    LastSavedJsonStorage().clear();
}

std::optional<std::filesystem::path> ActiveProjectSessionRoot() {
    const std::lock_guard<std::mutex> lock(ProjectSessionMutex());
    return ActiveRootStorage();
}

std::optional<ProjectSessionData> LoadActiveProjectSession() {
    std::optional<std::filesystem::path> root;
    {
        const std::lock_guard<std::mutex> lock(ProjectSessionMutex());
        if (!RestoreEnabledStorage() || !ActiveRootStorage()) {
            return std::nullopt;
        }
        root = ActiveRootStorage();
    }
    try {
        return LoadProjectSessionFile(ProjectSessionPath(*root));
    }
    catch (const std::exception&) {
        return std::nullopt;
    }
}

void SaveActiveProjectSession(const ProjectSessionData& data) {
    std::optional<std::filesystem::path> root;
    {
        const std::lock_guard<std::mutex> lock(ProjectSessionMutex());
        if (!RestoreEnabledStorage() || !ActiveRootStorage()) {
            return;
        }
        root = ActiveRootStorage();
    }

    try {
        const std::string json = ProjectSessionToJson(data, *root);
        {
            const std::lock_guard<std::mutex> lock(ProjectSessionMutex());
            if (json == LastSavedJsonStorage()) {
                return; // unchanged since this process last wrote it
            }
        }
        SaveProjectSessionFile(data, *root, ProjectSessionPath(*root));
        {
            const std::lock_guard<std::mutex> lock(ProjectSessionMutex());
            LastSavedJsonStorage() = json;
        }
    }
    catch (const std::exception&) {
        // Swallowed -- unattended 5s-tick save, nothing to report to (same
        // reasoning SaveFilePlaces documents).
    }
}

void ResetProjectSessionForTesting() {
    const std::lock_guard<std::mutex> lock(ProjectSessionMutex());
    ActiveRootStorage().reset();
    LastSavedJsonStorage().clear();
    RestoreEnabledStorage() = true;
}

} // namespace ned::editor
