#include "BufferSave.h"

#include "Backup.h"
#include "FinalNewline.h"
#include "LineEndingPolicy.h"
#include "TrimOnSave.h"

namespace ned::editor {

void WriteBufferToDisk(text::Buffer& buffer) {
    // binary-safety-guardrails follow-up: a buffer opened via a confirmed
    // "open anyway?" binary override gets none of the byte-level,
    // content-changing save-time behaviors below by default -- see
    // BinarySafeguardsActive()'s own doc comment.
    const bool binarySafeguards = buffer.BinarySafeguardsActive();

    // backup-and-recovery follow-up: preserve the file's prior on-disk
    // content before the save's rename clobbers it, and drop the
    // now-obsolete crash-recovery autosave once the save has actually
    // succeeded. Both swallow their own failures -- hooked here rather
    // than inside Buffer::Save so Text/ stays policy-free and scratch
    // auto-save (which calls Buffer::Save directly) never creates backup
    // versions.
    if (buffer.Path()) {
        BackupFileBeforeSave(*buffer.Path());
    }
    buffer.Save(EnsureFinalNewline() && !binarySafeguards, TrimTrailingWhitespaceOnSave() && !binarySafeguards,
                binarySafeguards ? std::optional<text::LineEnding>{}
                                  : std::optional<text::LineEnding>(ResolveLineEndingForSave(buffer.LineEndingKind())));
    if (buffer.Path()) {
        RemoveAutoSave(*buffer.Path());
    }
}

} // namespace ned::editor
