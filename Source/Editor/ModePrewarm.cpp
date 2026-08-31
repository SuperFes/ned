#include "ModePrewarm.h"

#include "HighlightSettings.h"
#include "ModeOverrides.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "UI/EventLoop.h"

namespace ned::editor {

Mode BuildWarmModeForPath(const std::filesystem::path& path, std::string_view text) {
    Mode mode = ModeForPath(path);
    if (mode.highlight && text.size() <= MaxHighlightBytes()) {
        mode.highlight(text);
        if (mode.fold) {
            // Shares the same tree-sitter parse highlight() above just
            // cached (Mode.cpp's SharedParse) -- see
            // TreeSitterModeFromLanguage's own comment on why highlight/
            // fold/expandSelection all key off the same cached tree.
            mode.fold(text);
        }
    }
    return mode;
}

ModePrewarmer::ModePrewarmer(text::BufferList& bufferList, ui::EventLoop& eventLoop)
    : bufferList_(bufferList), eventLoop_(eventLoop) {}

void ModePrewarmer::Prewarm(text::Buffer& buffer) {
    if (!buffer.Path() || inFlight_.contains(buffer.Name())) {
        return;
    }
    const std::string                        name     = buffer.Name();
    const std::filesystem::path              path     = *buffer.Path();
    std::unique_ptr<text::ITextStorage>      snapshot = buffer.Content().Clone(); // see header comment: O(1), thread-safe

    inFlight_[name] = std::jthread([this, name, path, snapshot = std::move(snapshot)](std::stop_token) {
        Mode mode = BuildWarmModeForPath(path, snapshot->ToString());
        eventLoop_.Post([this, name, mode = std::move(mode)]() mutable { ApplyPrewarmedMode(name, std::move(mode)); });
    });
}

void ModePrewarmer::ApplyPrewarmedMode(const std::string& bufferName, Mode mode) {
    inFlight_.erase(bufferName); // joins the now-finished (or finishing) background thread
    text::Buffer* stillOpen = bufferList_.Find(bufferName);
    if (stillOpen != nullptr) {
        InsertPrewarmedMode(*stillOpen, std::move(mode));
    }
}

} // namespace ned::editor
