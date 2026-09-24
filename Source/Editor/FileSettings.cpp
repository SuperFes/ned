#include "FileSettings.h"

#include <map>
#include <string>

#include "EditorConfig.h"
#include "IndentDetect.h"
#include "Modeline.h"
#include "Text/Buffer.h"

namespace ned::editor {

namespace {

    // Enough lines to settle a file's convention without reading all of a large one.
    constexpr std::size_t kDetectionHeadBytes = 256 * 1024;

    using Properties = std::map<std::string, std::string>;

    Properties EditorConfigFor(const std::filesystem::path& path) {
        return EditorConfigEnabled() ? EditorConfigPropertiesFor(path) : Properties{};
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
}

} // namespace ned::editor
