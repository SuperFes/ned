#include "PersistentUndo.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "ScratchPad.h"
#include "Session.h"

namespace ned::editor {

namespace {

    using Json = nlohmann::json;

    // NED_DEBUG_UNDO=<path>: heap-corruption-hunt follow-up. A real,
    // coredump-confirmed SIGABRT ("corrupted size vs. prev_size while
    // consolidating" -- glibc's malloc detecting a heap overflow, noticed
    // asynchronously by an unrelated later free()) landed with the reported
    // repro "edit, save, undo, save again, exit" -- exactly the shape of
    // this file's own save path, which also runs unattended on every
    // periodic autosave tick (WindowManager.cpp), not just at exit
    // (main.cpp's quit path), so the actual corrupting write could predate
    // the crash by an arbitrary amount of session time. Brackets every
    // risky step (buffer snapshot size, JSON serialization, disk write) so
    // the log's last lines at the moment of a future crash pinpoint which
    // buffer/size was mid-flight, same NED_DEBUG_MOUSE/NED_DEBUG_KEYS
    // env-var-to-file idiom as elsewhere in this codebase. Meant to be left
    // on across many real sessions until it happens again -- deliberately
    // verbose (one line per save attempt, not per keystroke) rather than
    // silent between crashes.
    void DebugLog(const std::string& line) {
        static FILE* log = []() -> FILE* {
            const char* path = std::getenv("NED_DEBUG_UNDO");
            return (path != nullptr && *path != '\0') ? std::fopen(path, "a") : nullptr;
        }();
        if (log == nullptr) {
            return;
        }
        std::fprintf(log, "%s\n", line.c_str());
        std::fflush(log);
    }

    // Duplicated from Backup.cpp/ProjectSession.cpp/ProjectTrust.cpp's own
    // private copies -- the same "not worth a shared dependency for
    // something this small" call each of those already made.
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

    std::mutex& UndoMutex() {
        static std::mutex mutex;
        return mutex;
    }

    bool& EnabledStorage() {
        static bool enabled = true;
        return enabled;
    }

    int& MaxSizeMbStorage() {
        static int megabytes = 16;
        return megabytes;
    }

    std::uintmax_t MaxUndoBytes() {
        const std::lock_guard<std::mutex> lock(UndoMutex());
        return static_cast<std::uintmax_t>(MaxSizeMbStorage()) * 1024 * 1024;
    }

    // NormalizePathKey string -> the ContentGeneration() last persisted for
    // that path -- SaveUndoHistoryForOpenBuffers' dirty-skip memo,
    // AutoSaveFileBuffers' own memo shape.
    std::unordered_map<std::string, std::size_t>& GenerationStorage() {
        static std::unordered_map<std::string, std::size_t> generations;
        return generations;
    }

    // Backup.cpp's own IsScratchFile, duplicated rather than shared for the
    // same reason Fnv1a64Hex is above -- ScratchPad.h's persistence already
    // owns scratch buffers, this subsystem stays out of their way.
    bool IsScratchFile(const std::filesystem::path& file) {
        try {
            return std::filesystem::weakly_canonical(file.parent_path()) ==
                   std::filesystem::weakly_canonical(ScratchDirectory());
        }
        catch (const std::exception&) {
            return false;
        }
    }

    bool Eligible(const text::Buffer& buffer) {
        if (!buffer.Path() || buffer.IsLoading() || buffer.ReadOnly()) {
            return false;
        }
        return !IsScratchFile(*buffer.Path());
    }

    void AtomicWrite(const std::filesystem::path& path, std::string_view content) {
        std::filesystem::create_directories(path.parent_path());

        const std::filesystem::path temporary = path.string() + ".ned-tmp";
        {
            std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
            if (!file) {
                throw std::runtime_error("ned: cannot write undo history to \"" + temporary.string() + "\"");
            }
            file.write(content.data(), static_cast<std::streamsize>(content.size()));
            if (!file.flush()) {
                throw std::runtime_error("ned: failed writing undo history to \"" + temporary.string() + "\"");
            }
        }
        std::filesystem::rename(temporary, path);
    }

    std::string SerializeToJson(const std::vector<text::UndoTree::SerializedNode>& nodes, std::size_t currentId,
                                const std::filesystem::path& path) {
        Json jsonNodes = Json::array();
        for (const auto& node : nodes) {
            Json entry = {
                {"id", node.id},
                {"parentId", node.parentId ? Json(*node.parentId) : Json(nullptr)},
                {"content", node.content},
                {"mostRecentChild", node.mostRecentChild},
            };
            jsonNodes.push_back(std::move(entry));
        }
        return Json{
            {"version", 1},
            {"path", path.string()},
            {"currentId", currentId},
            {"nodes", std::move(jsonNodes)},
        }
            .dump(2);
    }

    // Returns nullopt on any malformed/unparseable content -- a corrupt or
    // hand-edited undo file must never block opening the file it describes.
    std::optional<std::vector<text::UndoTree::SerializedNode>> ParseNodes(std::string_view json) {
        try {
            const Json                                  parsed = Json::parse(json);
            std::vector<text::UndoTree::SerializedNode> nodes;
            for (const Json& entry : parsed.at("nodes")) {
                text::UndoTree::SerializedNode node;
                node.id              = entry.at("id").get<std::size_t>();
                node.content         = entry.at("content").get<std::string>();
                node.mostRecentChild = entry.value("mostRecentChild", std::size_t{0});
                if (entry.contains("parentId") && !entry["parentId"].is_null()) {
                    node.parentId = entry["parentId"].get<std::size_t>();
                }
                nodes.push_back(std::move(node));
            }
            return nodes;
        }
        catch (const std::exception&) {
            return std::nullopt;
        }
    }

} // namespace

std::filesystem::path UndoDirectory() {
    if (const char* xdgStateHome = std::getenv("XDG_STATE_HOME"); xdgStateHome && *xdgStateHome) {
        return std::filesystem::path(xdgStateHome) / "ned" / "undo";
    }

    if (const char* home = std::getenv("HOME"); home && *home) {
        return std::filesystem::path(home) / ".local" / "state" / "ned" / "undo";
    }

    throw std::runtime_error("ned: cannot determine state directory (neither XDG_STATE_HOME nor HOME is set)");
}

std::filesystem::path UndoFileForPath(const std::filesystem::path& file) {
    return UndoDirectory() / (Fnv1a64Hex(FilePlaceStore::NormalizePathKey(file)) + ".json");
}

void SaveUndoHistory(const text::Buffer& buffer) {
    if (!PersistentUndoEnabled() || !Eligible(buffer)) {
        return;
    }
    if (buffer.Content().ByteLength() > MaxUndoBytes()) {
        return;
    }

    const auto nodes = buffer.SerializeUndo();
    if (nodes.size() <= 1) {
        return; // nothing beyond a fresh load already gives
    }

    DebugLog("SaveUndoHistory: begin path=" + buffer.Path()->string() +
             " bufferBytes=" + std::to_string(buffer.Content().ByteLength()) + " nodeCount=" + std::to_string(nodes.size()) +
             " currentId=" + std::to_string(buffer.CurrentUndoNodeId()));
    try {
        const std::string json = SerializeToJson(nodes, buffer.CurrentUndoNodeId(), *buffer.Path());
        DebugLog("SaveUndoHistory: serialized jsonBytes=" + std::to_string(json.size()));
        AtomicWrite(UndoFileForPath(*buffer.Path()), json);
        DebugLog("SaveUndoHistory: wrote OK path=" + buffer.Path()->string());
    }
    catch (const std::exception& e) {
        DebugLog(std::string("SaveUndoHistory: EXCEPTION (swallowed) ") + e.what());
        // Swallowed -- see this function's own doc comment.
    }
}

void TryRestoreUndoHistory(text::Buffer& buffer) {
    if (!PersistentUndoEnabled() || !Eligible(buffer)) {
        return;
    }

    try {
        std::ifstream file(UndoFileForPath(*buffer.Path()), std::ios::binary);
        if (!file) {
            return; // nothing persisted for this path
        }
        const std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        DebugLog("TryRestoreUndoHistory: path=" + buffer.Path()->string() + " fileBytes=" + std::to_string(content.size()));

        const auto parsed = ParseNodes(content);
        if (!parsed || parsed->empty()) {
            DebugLog("TryRestoreUndoHistory: no usable nodes parsed, leaving fresh tree");
            return;
        }
        DebugLog("TryRestoreUndoHistory: parsed nodeCount=" + std::to_string(parsed->size()));

        const std::string bufferText = buffer.Text();
        for (const auto& node : *parsed) {
            if (node.content == bufferText) {
                DebugLog("TryRestoreUndoHistory: restoring at node id=" + std::to_string(node.id));
                buffer.RestoreUndoTree(*parsed, node.id);
                DebugLog("TryRestoreUndoHistory: restored OK");
                return;
            }
        }
        // No node matches -- the file diverged from every known history
        // point since this was last persisted; leave the buffer's fresh
        // single-node tree alone, per this header's own doc comment.
        DebugLog("TryRestoreUndoHistory: no node matched current content, leaving fresh tree");
    }
    catch (const std::exception& e) {
        DebugLog(std::string("TryRestoreUndoHistory: EXCEPTION (swallowed) ") + e.what());
        // Swallowed -- a corrupt undo file must never block opening a file.
    }
}

void SaveUndoHistoryForOpenBuffers(text::BufferList& bufferList) {
    if (!PersistentUndoEnabled()) {
        return;
    }

    DebugLog("SaveUndoHistoryForOpenBuffers: sweep start, bufferCount=" + std::to_string(bufferList.Buffers().size()));
    for (const auto& buffer : bufferList.Buffers()) {
        if (!Eligible(*buffer)) {
            continue;
        }

        const std::string key        = FilePlaceStore::NormalizePathKey(*buffer->Path());
        const std::size_t generation = buffer->ContentGeneration();
        {
            const std::lock_guard<std::mutex> lock(UndoMutex());
            const auto                        memo = GenerationStorage().find(key);
            if (memo != GenerationStorage().end() && memo->second == generation) {
                continue; // unchanged since the last persist
            }
        }

        SaveUndoHistory(*buffer);

        const std::lock_guard<std::mutex> lock(UndoMutex());
        GenerationStorage()[key] = generation;
    }
    DebugLog("SaveUndoHistoryForOpenBuffers: sweep end");
}

void SetPersistentUndoEnabled(bool enabled) {
    const std::lock_guard<std::mutex> lock(UndoMutex());
    EnabledStorage() = enabled;
}

bool PersistentUndoEnabled() {
    const std::lock_guard<std::mutex> lock(UndoMutex());
    return EnabledStorage();
}

void SetPersistentUndoMaxSizeMb(int megabytes) {
    const std::lock_guard<std::mutex> lock(UndoMutex());
    MaxSizeMbStorage() = megabytes > 0 ? megabytes : 1;
}

int PersistentUndoMaxSizeMb() {
    const std::lock_guard<std::mutex> lock(UndoMutex());
    return MaxSizeMbStorage();
}

void ResetPersistentUndoForTesting() {
    const std::lock_guard<std::mutex> lock(UndoMutex());
    EnabledStorage()   = true;
    MaxSizeMbStorage() = 16;
    GenerationStorage().clear();
}

} // namespace ned::editor
