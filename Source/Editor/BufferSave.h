//
// buffer-list-panel-save follow-up: the disk-write half of save-buffer --
// backup-before-save, the write itself (final-newline/trim-trailing-
// whitespace/line-ending policy, each gated off for a buffer with binary
// safeguards active), and clearing any crash-recovery autosave. Factored
// out of Commands.cpp's save-buffer body so save-some-buffers,
// BufferView::RequestLspFormatThenSaveBuffer (which can't reuse the
// save-buffer command directly -- it needs an LSP round trip first), and
// BufferListPanel's mark-for-save batch execute (no CommandContext at all)
// don't each hand-roll their own copy of the same three-step sequence --
// RequestLspFormatThenSaveBuffer's own prior copy is exactly what motivated
// this extraction. Format-on-save stays call-site-specific (synchronous in
// one caller, LSP-async in another, irrelevant in the rest) and isn't
// folded in here.
//

#ifndef NED_EDITOR_BUFFERSAVE_H
#define NED_EDITOR_BUFFERSAVE_H

#include <filesystem>
#include <functional>
#include <string>

#include "Text/Buffer.h"
#include "Text/SavePlan.h"

namespace ned::editor {

// Whether a save may leave the main thread. A big enough write blocks the
// event loop for as long as it takes -- nothing repaints, no key is read --
// so a save the user is waiting on directly can be handed to a background
// thread instead. Every other caller wants the old behavior, because they
// report the outcome from the call itself: save-some-buffers counts
// successes and names failures, BufferListPanel executes a batch, and
// --format is a one-shot CLI run with no event loop to post back to. Hence
// the default.
enum class SaveDispatch {
    ForceSynchronous, // blocks until the file is on disk, then reports
    Automatic,        // may complete later, reporting through the mode line
};

// Requires buffer.Path() -- throws std::runtime_error otherwise, same as
// Buffer::Save's own contract. Callers are expected to catch and report,
// matching every existing save-buffer call site. With SaveDispatch::
// Automatic a large buffer may instead return before the file is written,
// having thrown nothing: the write is then in flight (buffer.IsSaving()),
// and its outcome arrives later.
void WriteBufferToDisk(text::Buffer& buffer, SaveDispatch dispatch = SaveDispatch::ForceSynchronous);

// Everything a save does outside the buffer: the pre-save backup version,
// the write, and dropping the now-obsolete crash-recovery autosave. Touches
// no Buffer and no editor state, so it is equally callable on a background
// thread -- which is the point, and why the asynchronous and synchronous
// paths share one copy of this sequence rather than each keeping their own.
// Throws std::runtime_error on a failed write; the backup and autosave
// steps swallow their own failures, as they always have.
void RunSavePlanWithBackup(const text::SavePlan& plan, const std::filesystem::path& bufferPath);

// What an asynchronous save needs handed to it. The plan owns its own
// content snapshot, so nothing here refers to the buffer except by name --
// which is how the completion finds it again, or discovers it was closed
// meanwhile.
struct AsyncSaveRequest {
    std::string                         bufferName;
    std::filesystem::path               bufferPath; // what the buffer reports afterwards
    text::SavePlan                      plan;
    std::shared_ptr<text::SaveProgress> progress;
};

// Installed by main.cpp once the event loop exists (nothing can post a
// completion back before then, so every save before that point runs
// synchronously whatever its size). Returns true if it took the save, in
// which case it is responsible for calling Buffer::FinishSave or
// Buffer::AbandonSave on the main thread when the write ends; false falls
// back to the synchronous path. Process-wide and mutex-guarded, the same
// shape the other process-wide settings here use.
void SetAsyncSaveDispatcher(std::function<bool(AsyncSaveRequest)> dispatcher);

// The size at or above which SaveDispatch::Automatic hands a save to that
// dispatcher. Deliberately not its own setting: this reuses
// text::AsyncLoadThreshold(), on the grounds that a file big enough to be
// worth loading off the main thread is big enough to be worth saving off it
// too, and the Janet surface is a 1.0 freeze commitment that shouldn't grow
// a knob nobody asked for.
[[nodiscard]] bool ShouldSaveAsynchronously(const text::Buffer& buffer);

} // namespace ned::editor

#endif // NED_EDITOR_BUFFERSAVE_H
