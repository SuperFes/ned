#include "RemoteBuffer.h"

#include "Editor/FileSettings.h"
#include "Editor/ModeOverrides.h"
#include "Editor/PersistentUndo.h"
#include "Text/BinaryDetect.h"
#include "Text/BufferList.h"

namespace ned::editor::remote {

std::size_t MaxRemoteFileBytes() {
    return static_cast<std::size_t>(text::HugeFileThreshold());
}

std::optional<std::string> FinishRemoteLoad(text::Buffer& placeholder, const FsResult<text::FileContents>& read) {
    const text::RemoteFile& file = *placeholder.Remote();
    if (!read) {
        if (read.error().code != host::fs_error::kNotFound) {
            return read.error().message;
        }
        placeholder.FinishLoad(text::Rope());
        placeholder.SetRemoteTimestamp(std::nullopt);
    }
    else {
        try {
            const text::Buffer decoded = text::Buffer::FromRemoteContents(file, *read);
            placeholder.FinishLoad(text::Rope(decoded.Text()), decoded.LineEndingKind(), decoded.FileCharset());
        }
        catch (const text::BinaryFileError& e) {
            return e.what();
        }
        placeholder.SetRemoteTimestamp(read->timestamp);
    }
    // The placeholder's mode was settled from its path alone.
    ClearModeCacheFor(placeholder);
    ApplyFileSettings(placeholder);
    TryRestoreUndoHistory(placeholder);
    return std::nullopt;
}

} // namespace ned::editor::remote
