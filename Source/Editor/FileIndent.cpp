#include "FileIndent.h"

#include <string>

#include "EditorConfig.h"
#include "IndentDetect.h"
#include "Modeline.h"
#include "Text/Buffer.h"

namespace ned::editor {

namespace {

    // Enough lines to settle a file's convention without reading all of a large one.
    constexpr std::size_t kDetectionHeadBytes = 256 * 1024;

} // namespace

IndentOverride FileIndentOverride(const std::filesystem::path& path, std::string_view text) {
    IndentOverride indent;
    if (IndentDetection()) {
        indent = DetectedIndentOverride(text);
    }
    if (EditorConfigEnabled()) {
        indent = indent.OverlaidWith(EditorConfigIndent(EditorConfigPropertiesFor(path)));
    }
    const Modeline modeline = ParseModeline(text);
    return indent.OverlaidWith(IndentOverride{.useTabs = modeline.useTabs, .width = modeline.width});
}

void ApplyFileIndent(text::Buffer& buffer) {
    if (!buffer.Path()) {
        return;
    }
    const std::filesystem::path& path = *buffer.Path();
    buffer.SetLocalIndent(FileIndentOverride(path, ReadFileEnds(path, kDetectionHeadBytes)));
}

} // namespace ned::editor
