//
// Writes one buffer to disk off the main/UI thread, so saving a very large
// file doesn't freeze the editor for however long the write takes. The
// mirror of AsyncFileLoader, and wired the same way: WindowManager installs
// a dispatcher (editor::SetAsyncSaveDispatcher) that spins one of these up
// per large save.
//
// Threading model matches AsyncFileLoader's exactly -- one background
// std::jthread does the filesystem work and only ever touches editor state
// through ned::ui::EventLoop::Post, never directly, and never holds a raw
// Buffer& across a Post boundary: it captures the buffer's name and looks
// it up again via BufferList::Find inside the posted completion, so a
// buffer closed while its write was still running is a safe no-op rather
// than a dangling reference. What the write itself reads is the plan's own
// content snapshot (Text/SavePlan.h), which shares nothing mutable with
// the live buffer, so the buffer stays fully editable throughout.
//
// One deliberate difference from the loader: this thread is never
// cancelled. A half-written save is a damaged file -- the temp-file path
// would leave a stray .ned-tmp, and the in-place path would leave the real
// file truncated -- so there is no stop_token check anywhere in the write,
// and shutdown waits for it (WindowManager::AsyncSaveInFlight, which
// main.cpp's quit path polls while telling the user what it's waiting on)
// rather than abandoning it.
//

#ifndef NED_UI_ASYNCFILESAVER_H
#define NED_UI_ASYNCFILESAVER_H

#include <string>
#include <thread>

#include "Editor/BufferSave.h"
#include "Text/BufferList.h"

namespace ned::ui {

class EventLoop;

class AsyncFileSaver {
  public:
    // The buffer named by request must already be IsSaving() (which
    // editor::WriteBufferToDisk's BeginSave has done by the time it hands
    // the request over). bufferList and eventLoop must outlive this saver.
    AsyncFileSaver(editor::AsyncSaveRequest request, text::BufferList& bufferList, EventLoop& eventLoop);
    ~AsyncFileSaver();

    AsyncFileSaver(const AsyncFileSaver&)            = delete;
    AsyncFileSaver& operator=(const AsyncFileSaver&) = delete;

    // Set on the main thread from inside the posted completion, once the
    // buffer's FinishSave/AbandonSave has run -- same poll-then-drop
    // contract AsyncFileLoader::Done() has, and the same reason for not
    // self-destructing from within that callback.
    [[nodiscard]] bool Done() const;

    // What the mode line says while this is running, e.g.
    // "Saving huge.log... 43%". Read on the main thread each frame; the
    // percentage comes from the buffer's own SaveProgress.
    [[nodiscard]] const std::string& BufferName() const;

    // Non-empty once a failed write has been reported, for the status line
    // to show. Cleared by nobody -- this object is dropped right after.
    [[nodiscard]] const std::string& Error() const;

  private:
    void Run(EventLoop& eventLoop);

    text::BufferList&     bufferList_;
    std::string           bufferName_;
    std::filesystem::path bufferPath_;
    text::SavePlan        plan_;
    std::string           error_;
    bool                  done_ = false;

    // Declared last so it is joined first. Nothing requests a stop -- see
    // this file's header comment on why a save is never cancelled.
    std::jthread thread_;
};

} // namespace ned::ui

#endif // NED_UI_ASYNCFILESAVER_H
