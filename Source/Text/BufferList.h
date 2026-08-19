//
// Owns a set of named Buffers, independent of any UI. Duplicate names are
// uniquified Emacs-style ("foo", "foo<2>", "foo<3>", ...) rather than
// silently colliding, e.g. when opening two files that share a basename.
//

#ifndef NED_TEXT_BUFFERLIST_H
#define NED_TEXT_BUFFERLIST_H

#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "Buffer.h"

namespace ned::text {

class BufferList {
  public:
    Buffer& CreateBuffer(std::string name);

    // Throws std::runtime_error if the file can't be opened for reading, or
    // text::BinaryFileError if it looks binary and allowBinary is false
    // (large-file-async-load / open-binary-anyway follow-ups -- see
    // Buffer::FromFile's own comment; checked here too, before any size
    // check, since a binary file should never even be considered for the
    // async path below). allowBinary is the explicit override a caller
    // opts into (a confirmed "open anyway?" prompt, a --force-binary CLI
    // flag) -- never set based on the file's own content.
    //
    // A file over kAsyncLoadThreshold (BufferList.cpp) is handled specially
    // if an async opener hook is set (SetAsyncFileOpener): rather than
    // blocking here for however long the read+Rope-build takes, this
    // returns immediately with an empty, IsLoading() placeholder buffer
    // that the hook is responsible for filling in over time -- the same
    // synchronous Buffer&-returning contract every caller already relies
    // on, just pointing at a buffer that isn't fully populated yet. With no
    // hook set (the default -- e.g. every test that constructs a bare
    // BufferList), this always behaves exactly as before: fully
    // synchronous, fully loaded on return.
    Buffer& OpenFile(const std::filesystem::path& path, bool allowBinary = false);

    // OpenFile if path exists; otherwise a new, empty buffer already
    // associated with path (Buffer::NewFile) -- e.g. `ned newfile.txt` for a
    // file that doesn't exist yet. Still throws for a real I/O failure on an
    // existing path (permissions, etc.), same as OpenFile.
    Buffer& OpenOrCreateFile(const std::filesystem::path& path, bool allowBinary = false);

    [[nodiscard]] Buffer*       Find(const std::string& name);
    [[nodiscard]] const Buffer* Find(const std::string& name) const;

    // Path-associated buffer already open, if any -- compares
    // std::filesystem::absolute() of both sides, so a relative and an
    // absolute path to the same file still match. A buffer with no
    // associated path (Path() == nullopt, e.g. a plain CreateBuffer scratch
    // buffer) never matches. Used by ProjectSidebar's click handler
    // (single-click-preview follow-up) to reuse an already-open buffer
    // instead of OpenFile unconditionally creating a uniquified duplicate.
    [[nodiscard]] Buffer*       FindByPath(const std::filesystem::path& path);
    [[nodiscard]] const Buffer* FindByPath(const std::filesystem::path& path) const;

    // Returns true if a buffer with that name was found and closed. Clears
    // PreviewBuffer() first if it happens to be the buffer being closed, so
    // that pointer never dangles.
    bool Close(const std::string& name);

    // The buffer currently shown as a transient "preview" (single-click-
    // preview follow-up), or nullptr if none. Self-clearing: if the
    // pointed-to buffer has become Modified() since it was marked (typing
    // into it, a replace operation touching it, anything that sets
    // Modified()), reading this returns nullptr and forgets it -- a preview
    // is promoted to a real, permanent buffer the instant it's actually
    // edited, no separate "did an edit happen" tracking needed anywhere
    // else. At most one buffer is ever the preview; setting a new one does
    // not affect whether the previous one still exists as a real buffer,
    // it just stops being *the preview* -- callers that want the classic
    // "replace, don't accumulate" preview behavior close the old preview
    // buffer themselves before opening the new one.
    [[nodiscard]] Buffer* PreviewBuffer() const;
    void                  SetPreviewBuffer(Buffer* buffer);

    [[nodiscard]] std::size_t                                 Count() const;
    [[nodiscard]] const std::vector<std::unique_ptr<Buffer>>& Buffers() const;

    // large-file-async-load follow-up: called by OpenFile with a freshly
    // created, IsLoading() placeholder Buffer& (already inserted into this
    // list -- stable-addressed, same as every other Buffer here) and the
    // path to load, whenever a file exceeds kAsyncLoadThreshold. Unset by
    // default -- a plain no-op-by-absence the same way TabBar::
    // SetOnCloseRequest/ProjectSidebar::SetOnBufferClosed default to
    // nothing, which is also what keeps BufferList itself UI-agnostic: it
    // knows nothing about EventLoop/threads, just that something else has
    // opted in to filling this buffer in over time. Source/UI/
    // AsyncFileLoader.h is what main.cpp actually wires in here.
    void SetAsyncFileOpener(std::function<void(Buffer&, const std::filesystem::path&)> hook);

  private:
    [[nodiscard]] std::string UniqueName(const std::string& base) const;

    std::vector<std::unique_ptr<Buffer>> buffers_;
    mutable Buffer*                      previewBuffer_ = nullptr; // see PreviewBuffer()

    std::function<void(Buffer&, const std::filesystem::path&)> asyncFileOpener_; // see SetAsyncFileOpener()
};

// Tab-completion candidates for find-file's prompt: directory entries under
// the last '/'-separated path component of prefix, sorted, with a trailing
// '/' appended to directory candidates (Emacs-style, so completing into a
// directory can be immediately Tab-completed again). Returns an empty list
// rather than throwing if the directory can't be listed (doesn't exist yet,
// no permission, etc.) -- this is UI completion, not a hard file operation.
[[nodiscard]] std::vector<std::string> CompleteFilePath(std::string_view prefix);

// Tab-completion candidates for switch-to-buffer's prompt: sorted, prefix-
// matched open buffer names. Mirrors CompleteCommandNames (Editor/Command.h).
[[nodiscard]] std::vector<std::string> CompleteBufferNames(const BufferList& bufferList, std::string_view prefix);

} // namespace ned::text

#endif // NED_TEXT_BUFFERLIST_H
