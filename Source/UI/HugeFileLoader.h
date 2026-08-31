//
// progressive-huge-file-load follow-up: loads one file over
// BufferList::HugeFileThreshold() into a Buffer off the main/UI thread, in
// chunk-groups, so opening a multi-GB file doesn't block input for however
// long the mmap scan takes and doesn't hold the whole file resident at once
// (see Text/PieceTable.h's FromFileRange/Concatenated, this file's own
// Run() comment, and Text/MappedFile.h's residency model). Wired into
// BufferList::SetAsyncHugeFileOpener by WindowManager::
// EnableAsyncHugeFileLoading -- see that method's own comment for the
// overall contract (a placeholder Buffer is created synchronously and
// handed here to fill in over time).
//
// Unlike AsyncFileLoader (the 16MB-1GB tier's own loader, which this
// otherwise mirrors closely), the placeholder stays genuinely editable
// throughout the load -- MarkLoading(false), not MarkLoading(true) -- so
// this loader has to cope with real user edits landing on the buffer in
// between two of its own callbacks; see AppendHugeLoadChunk's doc comment
// in Text/Buffer.h for why splicing at the buffer's current end is always
// correct regardless, and this file's own Run() comment for how an Undo()
// that rolls the load frontier back mid-load is detected instead of
// silently spliced past.
//
// Threading model mirrors AsyncFileLoader/LspClient exactly: one background
// std::jthread does the actual file I/O and PieceTable fragment building,
// and only ever touches shared editor state (the target Buffer, by way of
// BufferList) via ned::ui::EventLoop::Post -- never directly. The
// background thread never holds a raw Buffer& across a Post boundary: it
// captures the buffer's name and looks it up again via BufferList::Find
// inside each posted callback, so a buffer closed mid-load is simply a safe
// no-op on the next posted callback rather than a dangling reference.
//

#ifndef NED_UI_HUGEFILELOADER_H
#define NED_UI_HUGEFILELOADER_H

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <thread>

#include "Text/BufferList.h"

namespace ned::ui {

class EventLoop;

class HugeFileLoader {
  public:
    // placeholder must already be inserted into bufferList (BufferList::
    // OpenFile does this before calling the async-huge-opener hook) and
    // must already be IsLoading() -- MarkLoading(false) specifically, so it
    // stays editable. bufferList and eventLoop must outlive this loader --
    // both are process-lifetime objects owned by main.cpp, same assumption
    // AsyncFileLoader/LspManager make. allowBinary is the same explicit
    // override BufferList::OpenFile/Buffer::FromHugeFile already take --
    // when set, this loader skips the CR/CRLF-refusal scan entirely (see
    // Run()'s own comment), matching the already-shipped FromHugeFile fix
    // this feature builds on.
    HugeFileLoader(text::Buffer& placeholder, text::BufferList& bufferList, std::filesystem::path path, bool allowBinary,
                   EventLoop& eventLoop);
    ~HugeFileLoader();

    HugeFileLoader(const HugeFileLoader&)            = delete;
    HugeFileLoader& operator=(const HugeFileLoader&) = delete;

    // Set (on the main thread, from inside a Post()ed callback) once
    // loading has finished, failed, or been stopped -- WindowManager polls
    // this to know when it's safe to drop this loader. See AsyncFileLoader::
    // Done's own doc comment for why this isn't a self-destruct from within
    // the callback that sets it.
    [[nodiscard]] bool Done() const;

  private:
    void Run(std::stop_token stopToken, std::filesystem::path path, bool allowBinary, EventLoop& eventLoop);

    text::BufferList& bufferList_;
    std::string       bufferName_; // captured once, before the thread starts -- see this file's own header comment
    bool              done_ = false;

    // The byte length Storage_ is expected to have immediately after the
    // most recently landed chunk -- std::nullopt before the first chunk has
    // landed. Compared against the buffer's actual current length at the
    // top of every subsequent posted callback: Undo()/Redo() replace
    // Storage_ wholesale rather than patching it incrementally, so a user
    // undoing back past one or more load-append steps mid-load would
    // otherwise leave this loader splicing its next chunk onto a tree
    // that's shorter than expected -- silently and permanently dropping
    // whatever range was undone away from the visible buffer while
    // LoadProgress_ keeps reporting as if nothing happened. A mismatch here
    // stops the load instead. Touched only from inside Post()ed callbacks
    // (main thread) -- Run() itself (background thread) never reads or
    // writes this.
    std::optional<std::size_t> expectedByteLength_;

    // Shared with the placeholder Buffer (Buffer::SetLoadProgress) so
    // ModeLine can render a live percentage, same shape/contract as
    // AsyncFileLoader's own progress_.
    std::shared_ptr<text::LoadProgress> progress_ = std::make_shared<text::LoadProgress>();

    // Declared last so it's destroyed (and therefore stop_requested()+
    // joined) first -- AsyncFileLoader's own precedent. This loader's
    // blocking calls (a regular mmap'd read fault, effectively a plain
    // memory read once paged in) return on their own between chunk-groups
    // rather than needing to be interrupted, so the stop_token check
    // between groups is prompt enough; no stronger "unblock a stuck read"
    // trick needed.
    std::jthread thread_;
};

} // namespace ned::ui

#endif // NED_UI_HUGEFILELOADER_H
