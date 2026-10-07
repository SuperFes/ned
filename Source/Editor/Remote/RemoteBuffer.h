//
// Buffers holding a remote host's file (Text/RemoteFile.h): what a load or
// a save does with the host's answer, and the saver that carries a save to
// its host.
//

#ifndef NED_EDITOR_REMOTE_REMOTEBUFFER_H
#define NED_EDITOR_REMOTE_REMOTEBUFFER_H

#include <cstddef>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "Editor/BufferSave.h"
#include "Editor/Lifetime.h"
#include "Editor/PostQueue.h"
#include "Editor/Remote/Fs.h"
#include "Editor/Remote/Hosts.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"

namespace ned::editor::remote {

// The most a remote file may hold to be opened; past it the read is
// refused rather than held in memory.
[[nodiscard]] std::size_t MaxRemoteFileBytes();

// Fills a loading placeholder (BufferList::AddRemotePlaceholder) with a
// read's outcome: the file, or an empty new file when the host has none,
// then settles what an open settles from content alone: its mode,
// indentation and persisted undo history.
// Returns why not otherwise -- a failed read, or content that looks
// binary -- leaving the placeholder as it was.
[[nodiscard]] std::optional<std::string> FinishRemoteLoad(text::Buffer& placeholder, const FsResult<text::FileContents>& read);

enum class RemoteLoaded { Read,
                          Created };

// Whether a load filled its placeholder, why not otherwise, and whether
// the host simply had no such file.
struct RemoteLoadOutcome {
    std::expected<RemoteLoaded, std::string> loaded;
    bool                                     missing = false;
};

// Reads a loading placeholder's file from its host, connecting first when
// needed, and settles it with FinishRemoteLoad. A file the host doesn't
// have opens empty with createMissing, and fails otherwise. done runs on
// the main thread with the still-open buffer; a buffer closed meanwhile,
// or an expired token, ends the load silently.
void LoadRemoteBuffer(Hosts& hosts, text::BufferList& buffers, const text::Buffer& placeholder, bool createMissing,
                      LifetimeToken token, std::function<void(text::Buffer& buffer, RemoteLoadOutcome outcome)> done);

// revert-buffer for a loaded remote buffer: reads its file from its host
// again and takes it as one undoable step (Buffer::RevertFromRemote),
// decoded as charset when given. done gets why not, if it couldn't, with
// the buffer untouched; LoadRemoteBuffer's terms otherwise.
void ReloadRemoteBuffer(Hosts& hosts, text::BufferList& buffers, const text::Buffer& buffer, std::optional<text::Charset> charset,
                        LifetimeToken token, std::function<void(text::Buffer& buffer, std::optional<std::string> failure)> done);

// Settles a remote save (Buffer::BeginRemoteSave) with the host's answer:
// saved, at the host's new timestamp, or abandoned with why. A file that
// changed on the host since the buffer last saw it marks the buffer
// externally modified, so the next save asks before overwriting it.
[[nodiscard]] std::optional<std::string> FinishRemoteSave(text::Buffer& buffer, text::SavePlan plan, const FsResult<host::FileStat>& written);

// Carries remote saves (SetRemoteSaveDispatcher) to their hosts, connecting
// when needed, and settles each buffer once its host answers. report gets
// the outcome for the mode line. Main thread only.
class RemoteSaver {
  public:
    RemoteSaver(Hosts& hosts, text::BufferList& buffers, PostQueue& postQueue, std::function<void(std::string message)> report);

    RemoteSaver(const RemoteSaver&)            = delete;
    RemoteSaver& operator=(const RemoteSaver&) = delete;

    // Always takes the save, and never settles it before returning.
    bool Dispatch(RemoteSaveRequest request);

  private:
    void Finish(const std::shared_ptr<RemoteSaveRequest>& request, const FsResult<host::FileStat>& written);

    Hosts&                                   hosts_;
    text::BufferList&                        buffers_;
    PostQueue&                               postQueue_;
    std::function<void(std::string message)> report_;
    LifetimeGuard                            lifetime_;
};

} // namespace ned::editor::remote

#endif // NED_EDITOR_REMOTE_REMOTEBUFFER_H
