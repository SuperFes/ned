#include "HighlightCache.h"

#include <deque>
#include <string>

#include "SyntaxTheme.h"

namespace ned::editor {

namespace {

    struct Entry {
        const text::Buffer*                               buffer            = nullptr;
        // Buffer::InstanceId() alongside the raw pointer -- confirmed live
        // (a placement-new repro, see HighlightCacheTest.cpp): a Buffer with
        // no lifetime hook into this cache (a stack-local test Fixture,
        // e.g.) can be destroyed and a wholly unrelated later Buffer can be
        // constructed at the exact same address, with the exact same
        // contentGeneration/modeName/window this cache would otherwise
        // accept as a match -- a dangling-pointer cache hit that served one
        // buffer's spans for another's content. InstanceId() is unique for
        // the process lifetime and never reused, so it's what actually
        // tells "the same buffer, still alive" from "a coincidence."
        std::size_t                                       instanceId        = 0;
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

    const std::size_t instanceId        = buffer.InstanceId();
    const std::size_t contentGeneration = buffer.ContentGeneration();
    const std::size_t classGeneration   = CaptureClassGeneration();

    std::deque<Entry>& entries = Entries();
    for (Entry& entry : entries) {
        if (entry.buffer == &buffer && entry.instanceId == instanceId && entry.contentGeneration == contentGeneration &&
            entry.classGeneration == classGeneration && entry.modeName == mode.name &&
            entry.window.Contains(window)) {
            return entry.spans;
        }
    }

    // Miss: recompute, and drop any stale entry for this same address rather
    // than letting one buffer fill the whole cache with its own history --
    // keyed on the address alone here (not instanceId too), since a dead
    // buffer's own now-meaningless entry deserves eviction just as much as
    // a live one's stale entry does.
    std::erase_if(entries, [&buffer](const Entry& entry) { return entry.buffer == &buffer; });

    Entry fresh;
    fresh.buffer            = &buffer;
    fresh.instanceId        = instanceId;
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

void ForgetHighlightCacheBuffer(const text::Buffer& buffer) {
    std::erase_if(Entries(), [&buffer](const Entry& entry) { return entry.buffer == &buffer; });
}

} // namespace ned::editor
