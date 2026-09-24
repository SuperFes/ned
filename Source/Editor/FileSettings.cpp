#include "FileSettings.h"

#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "EditorConfig.h"
#include "IndentDetect.h"
#include "Modeline.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"

namespace ned::editor {

namespace {

    // Enough lines to settle a file's convention without reading all of a large one.
    constexpr std::size_t kDetectionHeadBytes = 256 * 1024;

    using Properties = std::map<std::string, std::string>;

    Properties EditorConfigFor(const std::filesystem::path& path) {
        return EditorConfigEnabled() ? EditorConfigPropertiesFor(path) : Properties{};
    }

    // Buffer::InstanceId -> the FileGeneration its settings were read at.
    std::mutex& AppliedMutex() {
        static std::mutex mutex;
        return mutex;
    }

    std::unordered_map<std::size_t, std::size_t>& AppliedGenerations() {
        static std::unordered_map<std::size_t, std::size_t> generations;
        return generations;
    }

    IndentOverride IndentFrom(std::string_view text, const Properties& editorConfig) {
        IndentOverride indent;
        if (IndentDetection()) {
            indent = DetectedIndentOverride(text);
        }
        indent                  = indent.OverlaidWith(EditorConfigIndent(editorConfig));
        const Modeline modeline = ParseModeline(text);
        return indent.OverlaidWith(IndentOverride{.useTabs = modeline.useTabs, .width = modeline.width});
    }

} // namespace

IndentOverride FileIndentOverride(const std::filesystem::path& path, std::string_view text) {
    return IndentFrom(text, EditorConfigFor(path));
}

void ApplyFileSettings(text::Buffer& buffer) {
    if (!buffer.Path()) {
        return;
    }
    const std::filesystem::path& path         = *buffer.Path();
    const Properties             editorConfig = EditorConfigFor(path);
    buffer.SetLocalIndent(IndentFrom(ReadFileEnds(path, kDetectionHeadBytes), editorConfig));
    buffer.SetConventions(EditorConfigConventions(editorConfig));
    const std::lock_guard<std::mutex> lock(AppliedMutex());
    AppliedGenerations()[buffer.InstanceId()] = buffer.FileGeneration();
}

void RefreshFileSettings(text::BufferList& bufferList) {
    std::vector<text::Buffer*> stale;
    {
        const std::lock_guard<std::mutex>            lock(AppliedMutex());
        std::unordered_map<std::size_t, std::size_t> live;
        for (const auto& buffer : bufferList.Buffers()) {
            const auto applied = AppliedGenerations().find(buffer->InstanceId());
            if (applied == AppliedGenerations().end()) {
                continue; // never file-backed when opened (a scratch buffer) -- nothing to refresh
            }
            live.emplace(applied->first, applied->second);
            if (applied->second != buffer->FileGeneration()) {
                stale.push_back(buffer.get());
            }
        }
        AppliedGenerations() = std::move(live); // forget killed buffers
    }
    for (text::Buffer* buffer : stale) {
        ApplyFileSettings(*buffer);
    }
}

} // namespace ned::editor
