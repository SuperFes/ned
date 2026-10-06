//
// Buffers holding a remote host's file (Text/RemoteFile.h): what a load
// does with the host's answer, apart from the connection that carried it.
//

#ifndef NED_EDITOR_REMOTE_REMOTEBUFFER_H
#define NED_EDITOR_REMOTE_REMOTEBUFFER_H

#include <cstddef>
#include <optional>
#include <string>

#include "Editor/Remote/Fs.h"
#include "Text/Buffer.h"

namespace ned::editor::remote {

// The most a remote file may hold to be opened; past it the read is
// refused rather than held in memory.
[[nodiscard]] std::size_t MaxRemoteFileBytes();

// Fills a loading placeholder (BufferList::AddRemotePlaceholder) with a
// read's outcome: the file, or an empty new file when the host has none.
// Returns why not otherwise -- a failed read, or content that looks
// binary -- leaving the placeholder as it was.
[[nodiscard]] std::optional<std::string> FinishRemoteLoad(text::Buffer& placeholder, const FsResult<text::FileContents>& read);

} // namespace ned::editor::remote

#endif // NED_EDITOR_REMOTE_REMOTEBUFFER_H
