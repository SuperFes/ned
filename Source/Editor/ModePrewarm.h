//
// background-mode-prewarm follow-up: builds a buffer's Mode -- tree-sitter
// Parser/Query construction plus one real parse -- on a background thread,
// ahead of the user ever switching to it, so CachedModeForBuffer's first
// real (synchronous, main-thread) lookup for that buffer finds an
// already-warm tree-sitter tree instead of paying for a full reparse right
// when the user is waiting on it.
//

#ifndef NED_EDITOR_MODEPREWARM_H
#define NED_EDITOR_MODEPREWARM_H

#include <filesystem>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>

#include "Mode.h"

namespace ned::text {
class Buffer;
class BufferList;
} // namespace ned::text

namespace ned::ui {
class EventLoop;
} // namespace ned::ui

namespace ned::editor {

// Resolves path's Mode (ModeForPath) and runs one real highlight()/fold()
// pass against text so the tree-sitter parse both closures share
// internally (Mode.cpp's TreeSitterModeFromLanguage, its own SharedParse)
// is already cached by the time this returns. Pure and synchronous -- no
// threading of its own -- so this is what a test exercises directly, the
// same "the actual logic is a plain function/method a test calls inline;
// the threaded wrapper is a thin, separately-covered shell around it"
// split TaskProcess::DispatchOutput/DispatchExit already establish.
// Respects editor::MaxHighlightBytes() exactly like BufferView::Paint's own
// highlighting gate -- a file too large to ever actually be highlighted
// shouldn't have a background thread spend real work parsing it anyway;
// same rule, uniformly, for every file, not a size-based special case
// invented just for prewarming.
[[nodiscard]] Mode BuildWarmModeForPath(const std::filesystem::path& path, std::string_view text);

// Owns one background std::jthread per in-flight prewarm, keyed by buffer
// name -- the same "map of jthread, erased from within a posted completion
// callback" shape TaskRunner/TaskProcess already establish
// (Editor/Tasks/TaskRunner.h). One instance is meant to live for the whole
// process, constructed once bufferList/eventLoop both exist; bufferList_/
// eventLoop_ are stored by reference on that assumption, the same one
// LspManager/DapManager/TaskRunner already make about both.
class ModePrewarmer {
  public:
    ModePrewarmer(text::BufferList& bufferList, ui::EventLoop& eventLoop);

    ModePrewarmer(const ModePrewarmer&)            = delete;
    ModePrewarmer& operator=(const ModePrewarmer&) = delete;

    // Safe, and cheap, to call for every buffer as it's opened, uniformly
    // -- including one about to be shown immediately (CachedModeForBuffer's
    // own synchronous resolution simply wins that race; InsertPrewarmedMode
    // never clobbers an entry that's already there) -- or one with no path
    // yet (a no-op: FundamentalMode has nothing worth prewarming) or one
    // already mid-prewarm (a no-op: this only ever runs one background
    // build per buffer name at a time).
    //
    // Snapshots buffer's content synchronously, on the calling thread,
    // before handing off (an O(1) text::Rope copy -- Rope.h's structural
    // sharing means this is just a shared_ptr copy, not a text copy). The
    // background thread only ever touches that private, immutable
    // snapshot, never buffer's own live Content() -- a concurrent edit to
    // buffer on the main thread while this prewarm is in flight is
    // therefore never a data race.
    void Prewarm(text::Buffer& buffer);

    // The background thread's completion callback's real body, exposed
    // directly (TaskProcess::DispatchOutput/DispatchExit's own precedent)
    // so a test can exercise "install the built Mode, unless the buffer
    // already closed" without a real thread and a live EventLoop::Run()
    // loop actually draining posted work.
    void ApplyPrewarmedMode(const std::string& bufferName, Mode mode);

  private:
    text::BufferList&                             bufferList_;
    ui::EventLoop&                                 eventLoop_;
    std::unordered_map<std::string, std::jthread> inFlight_;
};

} // namespace ned::editor

#endif // NED_EDITOR_MODEPREWARM_H
