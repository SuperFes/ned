//
// large-file-async-load follow-up: loads one file into a Buffer off the
// main/UI thread, in chunks, so opening a large legitimate text file
// doesn't block input for however long the read + Rope build takes. Wired
// into BufferList::SetAsyncFileOpener by main.cpp -- see that header's own
// comment for the overall contract (a placeholder Buffer is created
// synchronously and handed here to fill in over time).
//
// Threading model mirrors LspClient (Source/Editor/Lsp/LspClient.h) and
// WindowManager::StartAutoSaveTimer exactly: one background std::jthread
// does the actual file I/O and Rope building, and only ever touches shared
// editor state (the target Buffer, by way of BufferList) via
// ned::ui::EventLoop::Post -- never directly. The background thread never
// holds a raw Buffer& across a Post boundary: it captures the buffer's name
// and looks it up again via BufferList::Find inside each posted callback,
// so a buffer closed mid-load (the user hit the close-tab icon while a
// multi-gigabyte file was still loading) is simply a safe no-op on the next
// posted callback rather than a dangling reference.
//

#ifndef NED_UI_ASYNCFILELOADER_H
#define NED_UI_ASYNCFILELOADER_H

#include <filesystem>
#include <memory>
#include <string>
#include <thread>

#include "Text/BufferList.h"

namespace ned::ui {

class EventLoop;

class AsyncFileLoader {
  public:
    // placeholder must already be inserted into bufferList (BufferList::
    // OpenFile does this before calling the async-opener hook) and must
    // already be IsLoading(). bufferList and eventLoop must outlive this
    // loader -- both are process-lifetime objects owned by main.cpp, same
    // assumption LspManager/LspClient already make about EventLoop.
    AsyncFileLoader(text::Buffer& placeholder, text::BufferList& bufferList, std::filesystem::path path, EventLoop& eventLoop);
    ~AsyncFileLoader();

    AsyncFileLoader(const AsyncFileLoader&)            = delete;
    AsyncFileLoader& operator=(const AsyncFileLoader&) = delete;

    // Set (on the main thread, from inside the final Post()ed callback)
    // once loading has finished or failed -- WindowManager polls this to
    // know when it's safe to drop this loader. Deliberately not a
    // self-destruct from within the callback: destroying this object joins
    // the now-finished background thread, which is safe to do from the main
    // thread but awkward to trigger from a callback that's itself running
    // because that same thread posted it.
    [[nodiscard]] bool Done() const;

  private:
    void Run(std::stop_token stopToken, std::filesystem::path path, EventLoop& eventLoop);

    text::BufferList& bufferList_;
    std::string       bufferName_; // captured once, before the thread starts -- see this file's own header comment
    bool              done_ = false;

    // Shared with the placeholder Buffer (Buffer::SetLoadProgress) so
    // ModeLine can render a live percentage -- created and its totalBytes
    // written in the constructor, before thread_ ever starts; the thread
    // only ever touches the atomic bytesRead. See text::LoadProgress's own
    // doc comment for the full threading contract.
    std::shared_ptr<text::LoadProgress> progress_ = std::make_shared<text::LoadProgress>();

    // Declared last so it's destroyed (and therefore stop_requested()+
    // joined) first, per the usual reverse-declaration-order rule -- unlike
    // LspClient, nothing here needs the stronger "declared before an owned
    // resource whose destructor unblocks a stuck read" trick: this thread's
    // only blocking call is a plain regular-file read, which returns on its
    // own between chunks rather than needing to be interrupted, and the
    // stop_token is checked between chunks for a prompt-enough cancel.
    std::jthread thread_;
};

} // namespace ned::ui

#endif // NED_UI_ASYNCFILELOADER_H
