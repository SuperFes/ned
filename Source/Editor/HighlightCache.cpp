#include "HighlightCache.h"

#include <deque>
#include <string>

#include "SyntaxTheme.h"

namespace ned::editor {

namespace {

    struct Entry {
        const text::Buffer*                               buffer            = nullptr;
        std::size_t                                       contentGeneration = 0;
        std::size_t                                       classGeneration   = 0;
        std::string                                       modeName;
        HighlightWindow                                   window;
        std::shared_ptr<const std::vector<HighlightSpan>> spans;
    };

    // A handful of buffers, most-recently-used at the back. Small and linear
    // on purpose: this is only ever consulted from a paint, the working set
    // is the buffers actually on screen, and a pointer-keyed map would need
    // its own invalidation story when a Buffer dies.
    constexpr std::size_t kMaxEntries = 8;

    std::deque<Entry>& Entries() {
        static std::deque<Entry> entries;
        return entries;
    }

} // namespace

std::shared_ptr<const std::vector<HighlightSpan>> CachedHighlightSpans(const text::Buffer& buffer, const Mode& mode,
                                                                       HighlightWindow window) {
    static const auto kEmpty = std::make_shared<const std::vector<HighlightSpan>>();
    if (!mode.highlight) {
        return kEmpty;
    }

    const std::size_t contentGeneration = buffer.ContentGeneration();
    const std::size_t classGeneration   = CaptureClassGeneration();

    std::deque<Entry>& entries = Entries();
    for (Entry& entry : entries) {
        if (entry.buffer == &buffer && entry.contentGeneration == contentGeneration &&
            entry.classGeneration == classGeneration && entry.modeName == mode.name &&
            entry.window.Contains(window)) {
            return entry.spans;
        }
    }

    // Miss: recompute, and drop any stale entry for this same buffer rather
    // than letting one buffer fill the whole cache with its own history.
    std::erase_if(entries, [&buffer](const Entry& entry) { return entry.buffer == &buffer; });

    Entry fresh;
    fresh.buffer            = &buffer;
    fresh.contentGeneration = contentGeneration;
    fresh.classGeneration   = classGeneration;
    fresh.modeName          = mode.name;
    fresh.window            = window;
    fresh.spans             = std::make_shared<const std::vector<HighlightSpan>>(mode.highlight(buffer.Text(), window));

    entries.push_back(std::move(fresh));
    while (entries.size() > kMaxEntries) {
        entries.pop_front();
    }
    return entries.back().spans;
}

void ClearHighlightCache() {
    Entries().clear();
}

} // namespace ned::editor
