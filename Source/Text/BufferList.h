//
// Owns a set of named Buffers, independent of any UI. Duplicate names are
// uniquified Emacs-style ("foo", "foo<2>", "foo<3>", ...) rather than
// silently colliding, e.g. when opening two files that share a basename.
//

#ifndef NED_TEXT_BUFFERLIST_H
#define NED_TEXT_BUFFERLIST_H

#include <cstddef>
#include <cstdint>
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
    //
    // Dedupe-by-path (duplicate-open fix): if a buffer for this path is
    // already open (FindByPath), that buffer is returned as-is -- never a
    // second, uniquified "name<2>" duplicate of the same file. Emacs'
    // find-file-revisits-the-existing-buffer behavior, enforced here at
    // the one choke point rather than at each of the many call sites
    // (find-file's prompt, session restore, CLI args, LSP jumps, ...),
    // most of which never checked. Reload-from-disk is Buffer::Revert's
    // job, deliberately not this one's.
    Buffer& OpenFile(const std::filesystem::path& path, bool allowBinary = false);

    // OpenFile if path exists; otherwise a new, empty buffer already
    // associated with path (Buffer::NewFile) -- e.g. `ned newfile.txt` for a
    // file that doesn't exist yet. Still throws for a real I/O failure on an
    // existing path (permissions, etc.), same as OpenFile. Same
    // dedupe-by-path contract as OpenFile, including for a not-yet-saved
    // NewFile buffer whose path still doesn't exist on disk.
    Buffer& OpenOrCreateFile(const std::filesystem::path& path, bool allowBinary = false);

    [[nodiscard]] Buffer*       Find(const std::string& name);
    [[nodiscard]] const Buffer* Find(const std::string& name) const;

    // Path-associated buffer already open, if any -- compares
    // std::filesystem::weakly_canonical() of both sides (falling back to
    // absolute() when canonicalization fails), so a relative path, an
    // absolute one, and a "./sub/../sub/file" or symlinked spelling of the
    // same file all still match -- duplicate-open fix; was plain
    // absolute(), which treated those as different files. A buffer with no
    // associated path (Path() == nullopt, e.g. a plain CreateBuffer scratch
    // buffer) never matches. Used by OpenFile/OpenOrCreateFile's own
    // dedupe (above) and by ProjectSidebar's click handler (single-click-
    // preview follow-up).
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

    // MRU-close follow-up: records buffer as the most recently used. A
    // no-op for a buffer this list doesn't own (guards against a stale
    // pointer ever being recorded). ui::ActiveBuffer's on-change hook is
    // what actually calls this (wired per Pane in WindowManager) -- the one
    // choke point every buffer-switch path (tab click, find-file,
    // switch-to-buffer, sidebar preview, pane reassignment) already funnels
    // through -- so BufferList itself stays UI-agnostic, same as
    // SetOnFileOpened above.
    void TouchBuffer(const Buffer& buffer);

    // The most recently touched buffer other than excluding, or nullptr if
    // none has ever been touched (a buffer never activated -- e.g. opened
    // by session restore and never visited -- is not in the MRU order).
    // Close() purges its buffer from the order, so this never returns a
    // dangling pointer. BufferView::CloseBufferNow uses this to land on
    // the tab the user most recently left rather than the first tab.
    [[nodiscard]] Buffer* MostRecentlyUsedBuffer(const Buffer* excluding = nullptr) const;

    // Tab-reorder follow-up: moves buffer to targetIndex in Buffers()
    // order (clamped to the valid range), shifting the buffers in between.
    // Returns false for a buffer this list doesn't own. Buffers() order is
    // what TabBar renders and what SaveProjectSessionNow persists, so a
    // reorder both shows up immediately and survives a session restart.
    bool MoveBufferToIndex(const Buffer& buffer, std::size_t targetIndex);

    // session-persistence follow-up: called with every freshly opened
    // path-associated buffer, right before OpenFile/OpenOrCreateFile
    // returns it -- the one central seam every open path (CLI, find-file,
    // sidebar click, LSP jump, ...) already funnels through, so a caller
    // wanting to react to "a file buffer now exists" (main.cpp wires
    // save-place restore here) doesn't have to chase call sites. Unset by
    // default, same no-op-by-absence convention as SetAsyncFileOpener
    // below; also fires for an IsLoading() async placeholder and for a
    // not-yet-on-disk Buffer::NewFile buffer -- the hook decides what those
    // mean, not BufferList.
    void SetOnFileOpened(std::function<void(Buffer&)> hook);

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
    std::vector<Buffer*>                 mruOrder_;                // least recent first; see TouchBuffer()

    std::function<void(Buffer&, const std::filesystem::path&)> asyncFileOpener_; // see SetAsyncFileOpener()
    std::function<void(Buffer&)>                               onFileOpened_;    // see SetOnFileOpened()
};

// loose-ends follow-up: the file-size threshold (bytes) above which
// OpenFile hands a file to the async loader instead of reading it
// synchronously -- process-wide, mutex-guarded, default 16 MiB; configured
// from Janet via ned/set-async-load-threshold. Lives beside BufferList
// (its one consumer) rather than in Source/Editor/'s settings files, since
// the text layer must not depend on editor.
void                         SetAsyncLoadThreshold(std::uintmax_t bytes);
[[nodiscard]] std::uintmax_t AsyncLoadThreshold();

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
